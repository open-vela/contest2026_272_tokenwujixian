/****************************************************************************
 * chips/bk7258/audio/bk7258_adc.c
 *
 * BK7258 internal-ADC (analog-MIC capture) register-level HAL.  Public API is
 * in bk7258_adc.h.  The bring-up sequence mirrors
 * bk_avdk_smp/.../aud_adc_driver.c bk_aud_adc_init() and
 * aud_hal.h's register map:
 *
 *   subsystem init (shared, in bk7258_dac.c) -> clock select -> micen ->
 *   adcbias/micbias -> mic reset pulse -> ADC_CONFIG0 digital config ->
 *   sample-rate divider.
 *
 * Derived from Apache-2.0 code in bk_avdk_smp/:
 *   - middleware/driver/audio/aud/aud_adc_driver.c
 *   - middleware/soc/bk7258_ap/hal/sys_hal.c
 *   - middleware/soc/bk7258_ap/hal/aud_ll_macro_def.h
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "bk7258_internal.h"

#include "bk7258_aud_reg.h"
#include "bk7258_dac.h"
#include "bk7258_adc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Analog-reg indices shared with the DAC path (see bk7258_dac.c for the
 * offset derivation — n is the reg0-relative logical number, also the SPI
 * busy-bit number).
 */

#define BK7258_ANA_REG_APLL_PWD       5    /* ana_reg5, pwdaudpll bit 13. */
#define BK7258_ANA_REG18              18
#define BK7258_ANA_REG19              19
#define BK7258_ANA_REG25_APLL_CFG     25
#define BK7258_ANA_REG26_APLL_CAL     26

#define ANA_REG5_PWDAUDPLL_BIT        13
#define ANA_REG25_SPI_TRIGGER_BIT     18

/* Sys ctrl (0x44010000) clock registers/bit positions, taken from
 * bk_avdk_smp sys_struct.h for BK7258 (see bk7258_dac.c for full comment).
 */

#define SYS_CLK_DIV_MODE1             (BK7258_SYS_BASE + (0x8u * 4u))
#define   SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT   25
#define   SYS_CLK_DIV_MODE1_CKSEL_AUD_MASK  0x1u

#define SYS_CLK_DIV_MODE2             (BK7258_SYS_BASE + (0x9u * 4u))
#define   SYS_CLK_DIV_MODE2_DMIC_DIV_BIT    13

#define SYS_CPU_DEVICE_CLK_EN         (BK7258_SYS_BASE + (0xCu * 4u))
#define   SYS_CPU_DEVICE_CLK_AUD_CKEN_BIT   30

/* Default ADC-left FIFO write watermark.  AVDK media layer passes this per
 * buffer; 8 is a comfortable polled-capture watermark on BK7258.
 */

#define BK7258_ADC_ADCL_WR_TH_DEFAULT 8u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool       g_bk7258_adc_configured;
static bool       g_bk7258_adc_use_apll;
static spinlock_t g_bk7258_adc_lock = SP_UNLOCKED;

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

/* These are the same register-access primitives bk7258_dac.c uses.  They are
 * `static` in each translation unit (no cross-file symbol), so the identical
 * names are deliberate and read consistently across the two HAL files.
 */

static inline void bk7258_sys_reg_bits(uint32_t addr, uint32_t mask,
                                       uint32_t value)
{
  uint32_t v = getreg32(addr);
  v &= ~mask;
  v |= (value & mask);
  putreg32(v, addr);
}

static int bk7258_ana_reg_write(uint32_t idx, uint32_t value)
{
  const uint32_t addr = BK7258_SYS_ANA_REG(idx);
  int loops = 1000;

  putreg32(value, addr);

  while ((getreg32(BK7258_SYS_ANA_SPI_STATE) & (UINT32_C(1) << idx)) != 0)
    {
      if (--loops <= 0)
        {
          auderr("BK7258: analog reg %lu SPI wait timeout\n",
                 (unsigned long)idx);
          return -ETIMEDOUT;
        }
    }

  return OK;
}

static int bk7258_ana_reg_bit(uint32_t idx, uint32_t pos, uint32_t mask,
                              uint32_t value)
{
  const uint32_t addr = BK7258_SYS_ANA_REG(idx);
  uint32_t v = getreg32(addr);

  v &= ~(mask << pos);
  v |= ((value & mask) << pos);
  return bk7258_ana_reg_write(idx, v);
}

static int bk7258_apll_program(uint32_t cal_val)
{
  int ret;

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG_APLL_PWD,
                           ANA_REG5_PWDAUDPLL_BIT, 0x1u, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_write(BK7258_ANA_REG26_APLL_CAL, cal_val);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_write(BK7258_ANA_REG25_APLL_CFG,
                             BK7258_APLL_CONFIG_VAL);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG25_APLL_CFG,
                           ANA_REG25_SPI_TRIGGER_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(20);

  return bk7258_ana_reg_bit(BK7258_ANA_REG25_APLL_CFG,
                            ANA_REG25_SPI_TRIGGER_BIT, 0x1u, 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_adc_configure(const struct bk7258_adc_cfg_s *cfg)
{
  irqstate_t flags;
  uint32_t v;
  int ret;

  if (cfg == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_aud_subsystem_init();
  if (ret < 0)
    {
      return ret;
    }

  g_bk7258_adc_use_apll = cfg->use_apll;

  /* Clock select — identical to the DAC configure() clock block; the audio
   * clock mux is shared between the DAC and ADC paths.
   */

  if (cfg->use_apll)
    {
      bk7258_sys_reg_bits(SYS_CLK_DIV_MODE1,
                          SYS_CLK_DIV_MODE1_CKSEL_AUD_MASK
                              << SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT,
                          UINT32_C(1) << SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT);

      ret = bk7258_apll_program(BK7258_APLL_CAL_VAL_48K);
      if (ret < 0)
        {
          return ret;
        }

      v = getreg32(BK7258_AUD_AUDIO_CONFIG);
      v |= AUD_AUDIO_CFG_APLL_SEL;
      putreg32(v, BK7258_AUD_AUDIO_CONFIG);

      bk7258_sys_reg_bits(SYS_CLK_DIV_MODE2,
                          UINT32_C(1) << SYS_CLK_DIV_MODE2_DMIC_DIV_BIT,
                          UINT32_C(1) << SYS_CLK_DIV_MODE2_DMIC_DIV_BIT);
    }
  else
    {
      bk7258_sys_reg_bits(SYS_CLK_DIV_MODE1,
                          SYS_CLK_DIV_MODE1_CKSEL_AUD_MASK
                              << SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT,
                          0u);

      v = getreg32(BK7258_AUD_AUDIO_CONFIG);
      v &= ~AUD_AUDIO_CFG_APLL_SEL;
      putreg32(v, BK7258_AUD_AUDIO_CONFIG);
    }

  /* Analog MIC preamp enable.  On BK7258 only micen (ana_reg19[28]) is wired;
   * AVDK's mic2_en is a no-op, so dual-MIC shares this one bit.  Order matches
   * aud_adc_driver.c: micen -> biases -> reset pulse.
   */

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG19,
                           ANA_REG19_MICEN_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG18,
                           ANA_REG18_ENADCBias_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG18,
                           ANA_REG18_ENMICBias_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* MIC reset pulse after parameter config (aud_adc_driver.c:104..106). */

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG19,
                           ANA_REG19_MIC_RST_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(10);

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG19,
                           ANA_REG19_MIC_RST_BIT, 0x1u, 0);
  if (ret < 0)
    {
      return ret;
    }

  /* Digital path: gain, sample edge, HPF bypass. */

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_ADC_CONFIG0);
  v &= ~(AUD_ADC_CFG0_GAIN_MASK | AUD_ADC_CFG0_SAMP_EDGE
         | AUD_ADC_CFG0_HPF1_BYPASS | AUD_ADC_CFG0_HPF2_BYPASS);
  v |= ((uint32_t)(cfg->gain & 0x3Fu) << AUD_ADC_CFG0_GAIN_SHIFT);
  if (cfg->samp_edge)
    {
      v |= AUD_ADC_CFG0_SAMP_EDGE;
    }
  v |= AUD_ADC_CFG0_HPF1_BYPASS | AUD_ADC_CFG0_HPF2_BYPASS;
  putreg32(v, BK7258_AUD_ADC_CONFIG0);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);

  /* Sample-rate divider. */

  ret = bk7258_adc_set_sample_rate(cfg->samp_rate);
  if (ret < 0)
    {
      return ret;
    }

  bk7258_adc_set_adcl_wr_threshold(BK7258_ADC_ADCL_WR_TH_DEFAULT);

  g_bk7258_adc_configured = true;
  return OK;
}

int bk7258_adc_set_sample_rate(uint32_t samp_rate)
{
  uint32_t sr_code;
  bool     apll_44k_family = false;
  uint32_t v;
  int ret;

  switch (samp_rate)
    {
      case 8000:
        sr_code = AUD_SAMP_RATE_8K;
        break;

      case 16000:
        sr_code = AUD_SAMP_RATE_16K;
        break;

      case 44100:
        sr_code = AUD_SAMP_RATE_44_1K;
        apll_44k_family = true;
        break;

      case 48000:
        sr_code = AUD_SAMP_RATE_48K;
        break;

      default:
        auderr("BK7258: unsupported ADC sample rate %lu\n",
               (unsigned long)samp_rate);
        return -EINVAL;
    }

  /* On APLL, the 44.1k family requires a different cal value. */

  if (g_bk7258_adc_use_apll)
    {
      ret = bk7258_apll_program(apll_44k_family
                                    ? BK7258_APLL_CAL_VAL_44_1K
                                    : BK7258_APLL_CAL_VAL_48K);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* Standard rates use the auto divider: clear adc_fracmod_manual, then set
   * samp_rate_ADC [1:0] (distinct from samp_rate_DAC [7:6]).
   */

  v = getreg32(BK7258_AUD_EXTEND_CFG);
  v &= ~AUD_EXT_CFG_ADC_FRACMOD_MAN;
  putreg32(v, BK7258_AUD_EXTEND_CFG);

  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v &= ~AUD_AUDIO_CFG_SAMP_RATE_ADC_MASK;
  v |= (sr_code << AUD_AUDIO_CFG_SAMP_RATE_ADC_SHIFT)
       & AUD_AUDIO_CFG_SAMP_RATE_ADC_MASK;
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);

  return OK;
}

int bk7258_adc_set_gain(uint8_t gain)
{
  irqstate_t flags;
  uint32_t v;

  if (gain > 0x3Fu)
    {
      gain = 0x3Fu;
    }

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_ADC_CONFIG0);
  v &= ~AUD_ADC_CFG0_GAIN_MASK;
  v |= ((uint32_t)gain << AUD_ADC_CFG0_GAIN_SHIFT);
  putreg32(v, BK7258_AUD_ADC_CONFIG0);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);

  return OK;
}

void bk7258_adc_start(void)
{
  irqstate_t flags;
  uint32_t v;

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v |= (AUD_AUDIO_CFG_ADC_ENABLE | AUD_AUDIO_CFG_LINE_ENABLE);
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);
}

void bk7258_adc_stop(void)
{
  irqstate_t flags;
  uint32_t v;

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v &= ~(AUD_AUDIO_CFG_ADC_ENABLE | AUD_AUDIO_CFG_LINE_ENABLE);
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);
}

uintptr_t bk7258_adc_fifo_addr(void)
{
  return (uintptr_t)BK7258_AUD_ADC_FPORT;
}

uint32_t bk7258_adc_fifo_read(void)
{
  return getreg32(BK7258_AUD_ADC_FPORT);
}

void bk7258_adc_set_adcl_wr_threshold(uint32_t value)
{
  irqstate_t flags;
  uint32_t v;

  if (value > 0x1Fu)
    {
      value = 0x1Fu;
    }

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_FIFO_CONFIG);
  v &= ~AUD_FIFO_CFG_ADCL_WR_TH_MASK;
  v |= (value << AUD_FIFO_CFG_ADCL_WR_TH_SHIFT) & AUD_FIFO_CFG_ADCL_WR_TH_MASK;
  putreg32(v, BK7258_AUD_FIFO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);
}

bool bk7258_adc_fifo_empty(void)
{
  const uint32_t st = getreg32(BK7258_AUD_FIFO_STATUS);
  return (st & AUD_FIFO_ST_ADCL_EMPTY) != 0u;
}

void bk7258_adc_loop_adc2dac(bool enable)
{
  irqstate_t flags;
  uint32_t v;

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_FIFO_CONFIG);
  if (enable)
    {
      v |= AUD_FIFO_CFG_LOOP_ADC2DAC;
    }
  else
    {
      v &= ~AUD_FIFO_CFG_LOOP_ADC2DAC;
    }
  putreg32(v, BK7258_AUD_FIFO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);
}

void bk7258_aud_loop_ton2dac(bool enable)
{
  irqstate_t flags;
  uint32_t v;

  flags = spin_lock_irqsave(&g_bk7258_adc_lock);
  v = getreg32(BK7258_AUD_FIFO_CONFIG);
  if (enable)
    {
      v |= AUD_FIFO_CFG_LOOP_TON2DAC;
    }
  else
    {
      v &= ~AUD_FIFO_CFG_LOOP_TON2DAC;
    }
  putreg32(v, BK7258_AUD_FIFO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_adc_lock, flags);
}

void bk7258_adc_deinit(void)
{
  uint32_t v;

  bk7258_adc_stop();

  /* Drop analog MIC enables. */

  (void)bk7258_ana_reg_bit(BK7258_ANA_REG19, ANA_REG19_MICEN_BIT, 0x1u, 0);
  (void)bk7258_ana_reg_bit(BK7258_ANA_REG18, ANA_REG18_ENADCBias_BIT, 0x1u, 0);
  (void)bk7258_ana_reg_bit(BK7258_ANA_REG18, ANA_REG18_ENMICBias_BIT, 0x1u, 0);

  /* Restore digital defaults. */

  v = getreg32(BK7258_AUD_ADC_CONFIG0);
  v &= ~(AUD_ADC_CFG0_GAIN_MASK | AUD_ADC_CFG0_SAMP_EDGE
         | AUD_ADC_CFG0_HPF1_BYPASS | AUD_ADC_CFG0_HPF2_BYPASS);
  putreg32(v, BK7258_AUD_ADC_CONFIG0);

  /* Clear the ADC fracmod manual bit and drop to 8 kHz baseline. */

  v = getreg32(BK7258_AUD_EXTEND_CFG);
  v &= ~AUD_EXT_CFG_ADC_FRACMOD_MAN;
  putreg32(v, BK7258_AUD_EXTEND_CFG);

  /* Restore the vendor deinit clock baseline: XTAL + APLL down. */

  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v &= ~AUD_AUDIO_CFG_APLL_SEL;
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);
  bk7258_sys_reg_bits(SYS_CLK_DIV_MODE1,
                      SYS_CLK_DIV_MODE1_CKSEL_AUD_MASK
                          << SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT,
                      0u);
  bk7258_sys_reg_bits(SYS_CLK_DIV_MODE2,
                      UINT32_C(1) << SYS_CLK_DIV_MODE2_DMIC_DIV_BIT,
                      0u);
  (void)bk7258_ana_reg_bit(BK7258_ANA_REG_APLL_PWD,
                           ANA_REG5_PWDAUDPLL_BIT, 0x1u, 1u);

  g_bk7258_adc_use_apll   = false;
  g_bk7258_adc_configured = false;
}

void bk7258_adc_get_stats(struct bk7258_adc_stats_s *out)
{
  if (out == NULL)
    {
      return;
    }

  out->audio_config       = getreg32(BK7258_AUD_AUDIO_CONFIG);
  out->adc_config0        = getreg32(BK7258_AUD_ADC_CONFIG0);
  out->fifo_config        = getreg32(BK7258_AUD_FIFO_CONFIG);
  out->fifo_status        = getreg32(BK7258_AUD_FIFO_STATUS);
  out->extend_cfg         = getreg32(BK7258_AUD_EXTEND_CFG);
  out->clk_control        = getreg32(BK7258_AUD_CLK_CONTROL);
  out->sys_clk_div_mode1  = getreg32(SYS_CLK_DIV_MODE1);
  out->sys_clk_div_mode2  = getreg32(SYS_CLK_DIV_MODE2);
  out->device_clk_enable  = getreg32(SYS_CPU_DEVICE_CLK_EN);
  out->ana_reg18          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG18));
  out->ana_reg19          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG19));
}

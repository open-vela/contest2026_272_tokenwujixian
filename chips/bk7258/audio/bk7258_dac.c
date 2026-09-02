/****************************************************************************
 * chips/bk7258/audio/bk7258_dac.c
 *
 * BK7258 internal-DAC register-level HAL.  The public API is documented in
 * bk7258_dac.h; every ordering constraint, magic value and delay in this
 * file is traceable to a numbered step in
 * chips/bk7258/audio/BK7258_DAC_INIT_SEQUENCE.md.
 *
 * Derived from Apache-2.0 code in bk_avdk_smp/:
 *   - middleware/driver/audio/aud/aud_common_driver.c
 *   - middleware/driver/audio/aud/aud_dac_driver.c
 *   - middleware/soc/bk7258_ap/hal/sys_hal.c
 *   - middleware/soc/bk7258_ap/hal/sys_ll.h
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

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pre-computed ana_reg default values.  AVDK ships equivalent field-by-field
 * builders (aud_common_driver.c:153..291).  The hex constants below are the
 * comments AVDK itself left in place for reference, and match the runtime
 * value_cal() output field-for-field on BK7258 (SOC_BK7236XX branch).
 */

#define BK7258_ANA_REG18_DEFAULT      UINT32_C(0x00BF8085)
#define BK7258_ANA_REG19_DEFAULT      UINT32_C(0x81800006)
#define BK7258_ANA_REG20_DEFAULT      UINT32_C(0xFBC02423)
#define BK7258_ANA_REG21_DEFAULT      UINT32_C(0x00500000)
#define BK7258_ANA_REG27_DEFAULT      UINT32_C(0x91800006)

/* Analog-reg indices used by the DAC path.
 *
 * These are the AVDK "ana_reg N" logical register numbers, which are also
 * the word OFFSET from ana_reg0 (SYS_ANA_REG0_ADDR = SOC_SYS_REG_BASE +
 * (0x40<<2)).  AVDK addresses e.g. ana_reg5 at SOC_SYS_REG_BASE + (0x45<<2)
 * = reg0 + (5<<2); so BK7258_SYS_ANA_REG(n) = (reg0 base) + n*4 with
 * BK7258_SYS_ANALOG_REG_BASE already pointing at reg0 (word 0x40) gives the
 * correct physical address when n is the logical number (5, not 0x45).
 *
 * The same n is the SYS_ANALOG_REG_SPI_STATE busy bit number
 * (vendor: SYS_ANALOG_REG_SPI_STATE_POS(idx) = idx where idx is the
 * reg0-relative word offset), so bk7258_ana_reg_write()'s "1 << idx"
 * busy-wait matches vendor.  Passing the absolute word index (0x45 etc.)
 * here is WRONG — it double-counts the reg0 offset and writes 0x100 bytes
 * past the real register (verified: all ana_reg read back 0 in -F).
 */
#define BK7258_ANA_REG_APLL_PWD       5    /* AVDK "ana_reg5", pwdaudpll bit 13. */
#define BK7258_ANA_REG18              18
#define BK7258_ANA_REG19              19
#define BK7258_ANA_REG20              20
#define BK7258_ANA_REG21              21
#define BK7258_ANA_REG25_APLL_CFG     25
#define BK7258_ANA_REG26_APLL_CAL     26
#define BK7258_ANA_REG27              27

/* ana_reg20 bit positions we RMW at runtime.  All widths are 1 bit unless
 * annotated.  Values traced from sys_ll.h.
 */

#define ANA_REG20_DACDRVEN_BIT        19
#define ANA_REG20_DACLEN_BIT          21
#define ANA_REG20_DACG_BIT            22    /* 4-bit field.                */
#define ANA_REG20_DACG_MASK           0xFu
#define ANA_REG20_DACMUTE_BIT         26
#define ANA_REG20_DIFFEN_BIT          13
#define ANA_REG20_LENDCOC_BIT         16

/* ana_reg21 bit positions. */

#define ANA_REG21_ENIDACL_BIT         18
#define ANA_REG21_ENBS_BIT            23  /* sys_reg.h: SYS_ANA_REG21_ENBS_POS */

/* ana_reg18 bit positions. */

#define ANA_REG18_ENAUDBIAS_BIT       3

/* ana_reg5[13] = pwdaudpll (active-high power-DOWN). */

#define ANA_REG5_PWDAUDPLL_BIT        13

/* ana_reg25[18] = APLL SPI trigger. */

#define ANA_REG25_SPI_TRIGGER_BIT     18

/* Sys ctrl (0x44010000) register offsets used at runtime. */

/* Sys ctrl register bit positions taken from the AVDK sys_struct.h layout
 * for BK7258 (bk_avdk_smp/ap/middleware/soc/bk7258_ap/soc/sys_struct.h).
 */

#define SYS_CLK_DIV_MODE1             (BK7258_SYS_BASE + (0x8u * 4u))
#define   SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT   25  /* sys_struct.h:144 */
#define   SYS_CLK_DIV_MODE1_CKSEL_AUD_MASK  0x1u

#define SYS_CLK_DIV_MODE2             (BK7258_SYS_BASE + (0x9u * 4u))
#define   SYS_CLK_DIV_MODE2_DMIC_DIV_BIT    13  /* sys_struct.h:163 */

#define SYS_CPU_DEVICE_CLK_EN         (BK7258_SYS_BASE + (0xCu * 4u))
#define   SYS_CPU_DEVICE_CLK_AUD_CKEN_BIT   30  /* sys_struct.h:238 */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool           g_bk7258_aud_subsys_ready;
static bool           g_bk7258_dac_configured;
static spinlock_t     g_bk7258_aud_lock = SP_UNLOCKED;

/* Cached configuration so bk7258_dac_set_sample_rate() can honour the APLL
 * cal-value swap for the 44.1k family.
 */

static bool           g_bk7258_dac_use_apll;

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

/* Read-modify-write on a plain sys-ctrl register (no analog SPI).  A single
 * putreg32 is enough because the whole SoC AHB slave is coherent from the
 * CPU.  Called from init only; no interlock with IRQ paths.
 */

static inline void bk7258_sys_reg_bits(uint32_t addr, uint32_t mask,
                                       uint32_t value)
{
  uint32_t v = getreg32(addr);
  v &= ~mask;
  v |= (value & mask);
  putreg32(v, addr);
}

/* Analog-register write, mirrors sys_ll_set_analog_reg_value():
 *   REG_WRITE(addr, value);
 *   spin until SPI_STATE bit[idx] clears.
 *
 * The SPI transfer takes only a handful of AHB cycles; AVDK does not bound
 * the wait.  We keep a generous 1000-iteration cap so a stuck bus surfaces
 * as an error rather than a lock-up.
 */

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

/* Read-modify-write on an analog register bit (equivalent to
 * sys_set_ana_reg_bit() in AVDK sys_ll.h).
 */

static int bk7258_ana_reg_bit(uint32_t idx, uint32_t pos, uint32_t mask,
                              uint32_t value)
{
  const uint32_t addr = BK7258_SYS_ANA_REG(idx);
  uint32_t v = getreg32(addr);

  v &= ~(mask << pos);
  v |= ((value & mask) << pos);
  return bk7258_ana_reg_write(idx, v);
}

/* One-shot APLL bring-up (spec §2.2).  cal_val is the family-specific cal
 * word; caller decides based on sample rate.
 */

static int bk7258_apll_program(uint32_t cal_val)
{
  int ret;

  /* Clear the "power-down APLL" bit — active-high in ana_reg5[13]. */

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

  /* SPI trigger pulse.  AVDK loops delay(10); we use 200 us as a
   * conservative equivalent while APLL locks.
   */

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG25_APLL_CFG,
                           ANA_REG25_SPI_TRIGGER_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* AVDK loops delay(10) here (approximately 10 ms on their RTOS tick).
   * Waiting only a couple hundred microseconds is not enough for the APLL
   * to lock, and the DAC clock silently falls back to a divided XTAL — the
   * DAC block appears configured but the FIFO stops draining after a few
   * words.  10 ms matches the reference bring-up.
   */

  up_mdelay(20);

  return bk7258_ana_reg_bit(BK7258_ANA_REG25_APLL_CFG,
                            ANA_REG25_SPI_TRIGGER_BIT, 0x1u, 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_aud_subsystem_init(void)
{
  irqstate_t flags;
  int ret = OK;

  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  if (g_bk7258_aud_subsys_ready)
    {
      spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);
      return OK;
    }

  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);

  /* CP owns the shared SYS power and SRAM controls and clears them while AP
   * is held in reset.  AP must only validate that handoff: writing these
   * registers here would race CP power management through whole-word RMWs.
   * Fail closed before publishing subsystem readiness if the handoff did not
   * take effect.
   */

  {
    uint32_t power = getreg32(BK7258_SYS_POWER_WAKEUP);
    uint32_t mem_ctrl1 = getreg32(BK7258_SYS_DEV_MEM_CTRL1);
    uint32_t mem_ctrl2 = getreg32(BK7258_SYS_DEV_MEM_CTRL2);

    if ((power & BK7258_SYS_AUDP_POWER_DOWN) != 0 ||
        (mem_ctrl1 & BK7258_SYS_AUD_MEM_LOW_POWER) != 0 ||
        (mem_ctrl2 & BK7258_SYS_AUD_MEM_LOW_POWER) != 0)
      {
        auderr("BK7258: CP audio wake handoff invalid: "
               "pwr=%08lx sd=%08lx ds=%08lx\n",
               (unsigned long)power,
               (unsigned long)mem_ctrl1,
               (unsigned long)mem_ctrl2);
        return -EIO;
      }
  }

  /* §2.1 step 2/3: start from XTAL and gate the audio clock on.  The audio
   * clock mux is programmed later by bk7258_dac_configure().
   */

  bk7258_sys_reg_bits(SYS_CLK_DIV_MODE1,
                      SYS_CLK_DIV_MODE1_CKSEL_AUD_MASK
                          << SYS_CLK_DIV_MODE1_CKSEL_AUD_BIT,
                      0u);
  bk7258_sys_reg_bits(SYS_CPU_DEVICE_CLK_EN,
                      UINT32_C(1) << SYS_CPU_DEVICE_CLK_AUD_CKEN_BIT,
                      UINT32_C(1) << SYS_CPU_DEVICE_CLK_AUD_CKEN_BIT);

  /* §2.1 step 4: the AUD-block interrupt line is intentionally NOT gated
   * here.  Playback goes through GDMA; the driver does not attach any
   * handler to BK7258_IRQ_AUDIO, so leaving the sys-ctrl CPU int_en bit
   * clear avoids delivering an aggregate audio IRQ that nobody consumes.
   */

  /* §2.1 step 5: release the audio data path from reset via AUD_CLK_CONTROL.
   *
   * soft_reset is ACTIVE-LOW on this silicon: 1 = reset released (operating),
   * 0 = reset held.  AVDK's bk_aud_driver_init() does exactly
   * aud_hal_set_clk_control_soft_reset(1) and never clears bit0 until the
   * deinit path (which pulses 0->1->0 to re-assert reset).  The register
   * comment "写1复位...需要软件清除" is misleading; the reference code is the
   * ground truth.
   *
   * The earlier bring-up here pulsed bit0 1->0, leaving soft_reset=0 and so
   * HOLDING the data path in reset: the -F probe showed FIFO_STATUS staying
   * all-EMPTY across 32 rapid DAC_FPORT writes (the write port is dead in
   * reset), while ana_reg SPI writes still latched correctly (the analog SPI
   * runs on its own domain).  Leaving soft_reset=1, exactly as AVDK does,
   * restores the FIFO.
   *
   * bit1 (clk_gate) is intentionally left at its working default (0); AVDK
   * never writes it either.
   */

  putreg32(getreg32(BK7258_AUD_CLK_CONTROL) | AUD_CLK_CTRL_SOFT_RESET,
           BK7258_AUD_CLK_CONTROL);

  /* §2.1 step 6: apply the pre-computed ana_reg defaults.  These are the
   * exact values AVDK's ana_regNN_value_cal() functions produce on
   * SOC_BK7236XX; changing them alters bias current, VCM and DAC common
   * mode and MUST be re-derived from silicon test data if touched.
   */

  ret = bk7258_ana_reg_write(BK7258_ANA_REG18, BK7258_ANA_REG18_DEFAULT);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_ana_reg_write(BK7258_ANA_REG19, BK7258_ANA_REG19_DEFAULT);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_ana_reg_write(BK7258_ANA_REG20, BK7258_ANA_REG20_DEFAULT);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_ana_reg_write(BK7258_ANA_REG21, BK7258_ANA_REG21_DEFAULT);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_ana_reg_write(BK7258_ANA_REG27, BK7258_ANA_REG27_DEFAULT);
  if (ret < 0)
    {
      goto fail;
    }

  /* §2.1 step 7: enable the audio bias network AFTER the ana_reg defaults
   * have latched.
   */

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG18,
                           ANA_REG18_ENAUDBIAS_BIT, 0x1u, 1);
  if (ret < 0)
    {
      goto fail;
    }

  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  g_bk7258_aud_subsys_ready = true;
  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);

  audinfo("BK7258: audio subsystem initialised\n");
  return OK;

fail:
  auderr("BK7258: audio subsystem init failed: %d\n", ret);
  return ret;
}

int bk7258_dac_configure(const struct bk7258_dac_cfg_s *cfg)
{
  irqstate_t flags;
  uint32_t v;
  int ret;

  if (cfg == NULL)
    {
      return -EINVAL;
    }

  if (!g_bk7258_aud_subsys_ready)
    {
      ret = bk7258_aud_subsystem_init();
      if (ret < 0)
        {
          return ret;
        }
    }

  g_bk7258_dac_use_apll = cfg->use_apll;

  /* §2.2 clock select. */

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

      /* Divide DMIC clock so it does not steal APLL bandwidth. */

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

  /* §2.3 step 4: DAC analog path power-up.  On BK7258 (SOC_BK7236XX) the
   * BK7256-era right-channel/vdd rails are not-supported no-ops in AVDK;
   * we only drive the fields that actually map to silicon.
   *
   *   dac_bias_en  → ana_reg21[enbs]        (not exposed here, latched by
   *                                          the ana_reg21 default value)
   *   dac_drv_en   → ana_reg20[dacdrven]
   *   dac_dcoc_en  → ana_reg20[lendcoc]
   *   dac_idac_en  → ana_reg21[enidacl]
   *   dacl_en      → ana_reg20[daclen]
   *   diffen       → ana_reg20[diffen]
   */

  /* AVDK aud_dac_driver.c:81..87 explicitly flips these four bits in this
   * order.  ana_reg21.enbs (dac bias) is NOT part of the reset default
   * value we programmed earlier and MUST be raised now.
   */

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG21,
                           ANA_REG21_ENBS_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG20,
                           ANA_REG20_DACDRVEN_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG20,
                           ANA_REG20_LENDCOC_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG21,
                           ANA_REG21_ENIDACL_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG20,
                           ANA_REG20_DACLEN_BIT, 0x1u, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* §2.3 step 6: differential vs single-ended. */

  ret = bk7258_ana_reg_bit(BK7258_ANA_REG20,
                           ANA_REG20_DIFFEN_BIT, 0x1u,
                           cfg->work_mode == BK7258_DAC_WORK_DIFFEN
                                             ? 1u : 0u);
  if (ret < 0)
    {
      return ret;
    }

  /* §2.3 step 7: DAC digital path.  Gain, HPF bypass, clk invert. */

  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  v = getreg32(BK7258_AUD_DAC_CONFIG0);
  v &= ~(AUD_DAC_CFG0_GAIN_MASK | AUD_DAC_CFG0_CLK_INVERT
         | AUD_DAC_CFG0_HPF1_BYPASS | AUD_DAC_CFG0_HPF2_BYPASS);
  v |= ((uint32_t)(cfg->gain & 0x3Fu) << AUD_DAC_CFG0_GAIN_SHIFT);
  v |= AUD_DAC_CFG0_HPF1_BYPASS | AUD_DAC_CFG0_HPF2_BYPASS;
  putreg32(v, BK7258_AUD_DAC_CONFIG0);
  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);

  /* §2.4: FIFO read threshold.  With threshold=0 the audio block only
   * asserts a DMA request when the FIFO is completely empty; on a 32-word
   * FIFO that is one request per 32 samples, and combined with any DMA
   * arbitration latency the FIFO under-runs and stops requesting.  AVDK
   * uses 4 for BK7256; 8 leaves comfortable headroom on BK7258.
   */

  {
    uint32_t fifo_cfg;
    fifo_cfg  = getreg32(BK7258_AUD_FIFO_CONFIG);
    fifo_cfg &= ~(AUD_FIFO_CFG_DACL_RD_TH_MASK
                  | AUD_FIFO_CFG_DACR_RD_TH_MASK);
    fifo_cfg |= (UINT32_C(8) << AUD_FIFO_CFG_DACL_RD_TH_SHIFT);
    fifo_cfg |= (UINT32_C(8) << AUD_FIFO_CFG_DACR_RD_TH_SHIFT);
    putreg32(fifo_cfg, BK7258_AUD_FIFO_CONFIG);
  }

  /* §2.3 step 8: sample-rate divider. */

  ret = bk7258_dac_set_sample_rate(cfg->samp_rate);
  if (ret < 0)
    {
      return ret;
    }

  /* §2.3 step 9: previously we muted the DAC here.  Empirically the DAC
   * FIFO then never fully drains after start() unmutes; leaving the DAC
   * un-muted through configure() lets DMA replenish the FIFO immediately.
   */

  g_bk7258_dac_configured = true;
  return OK;
}

int bk7258_dac_set_sample_rate(uint32_t samp_rate)
{
  uint32_t sr_code;
  uint32_t fracmod = 0;
  bool     fracmod_manual = false;
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

      case 11025:
        sr_code = AUD_SAMP_RATE_44_1K;
        fracmod = BK7258_AUD_CONST_DIV_44_1K << 2;
        fracmod_manual = true;
        apll_44k_family = true;
        break;

      case 22050:
        sr_code = AUD_SAMP_RATE_44_1K;
        fracmod = BK7258_AUD_CONST_DIV_44_1K << 1;
        fracmod_manual = true;
        apll_44k_family = true;
        break;

      case 12000:
        sr_code = AUD_SAMP_RATE_48K;
        fracmod = BK7258_AUD_CONST_DIV_48K << 2;
        fracmod_manual = true;
        break;

      case 24000:
        sr_code = AUD_SAMP_RATE_48K;
        fracmod = BK7258_AUD_CONST_DIV_48K << 1;
        fracmod_manual = true;
        break;

      case 32000:
        sr_code = AUD_SAMP_RATE_16K;
        fracmod = BK7258_AUD_CONST_DIV_16K >> 1;
        fracmod_manual = true;
        break;

      default:
        auderr("BK7258: unsupported sample rate %lu\n",
               (unsigned long)samp_rate);
        return -EINVAL;
    }

  /* On APLL, the 44.1k family requires a different cal value.  Re-run the
   * APLL bring-up transparently.
   */

  if (g_bk7258_dac_use_apll)
    {
      ret = bk7258_apll_program(apll_44k_family
                                    ? BK7258_APLL_CAL_VAL_44_1K
                                    : BK7258_APLL_CAL_VAL_48K);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* Program fracmod first (upper half), then the samp_rate_DAC bits.  This
   * mirrors AVDK ordering and avoids a transient rate mismatch on the DAC
   * clock.
   */

  v = getreg32(BK7258_AUD_EXTEND_CFG);
  v &= ~AUD_EXT_CFG_DAC_FRACMOD_MAN;
  if (fracmod_manual)
    {
      v |= AUD_EXT_CFG_DAC_FRACMOD_MAN;
    }

  putreg32(v, BK7258_AUD_EXTEND_CFG);

  if (fracmod_manual)
    {
      putreg32(fracmod, BK7258_AUD_DAC_FRACMOD);
    }

  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v &= ~AUD_AUDIO_CFG_SAMP_RATE_MASK;
  v |= (sr_code << AUD_AUDIO_CFG_SAMP_RATE_SHIFT)
       & AUD_AUDIO_CFG_SAMP_RATE_MASK;
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);

  return OK;
}

void bk7258_dac_set_gain(uint8_t gain)
{
  irqstate_t flags;
  uint32_t v;

  if (gain > 0x3Fu)
    {
      gain = 0x3Fu;
    }

  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  v = getreg32(BK7258_AUD_DAC_CONFIG0);
  v &= ~AUD_DAC_CFG0_GAIN_MASK;
  v |= ((uint32_t)gain << AUD_DAC_CFG0_GAIN_SHIFT);
  putreg32(v, BK7258_AUD_DAC_CONFIG0);
  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);
}

int bk7258_dac_mute(bool mute)
{
  return bk7258_ana_reg_bit(BK7258_ANA_REG20,
                            ANA_REG20_DACMUTE_BIT, 0x1u,
                            mute ? 1u : 0u);
}

void bk7258_dac_start(void)
{
  irqstate_t flags;
  uint32_t v;

  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v |= AUD_AUDIO_CFG_DAC_ENABLE;
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);
}

void bk7258_dac_stop(void)
{
  irqstate_t flags;
  uint32_t v;

  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  v = getreg32(BK7258_AUD_AUDIO_CONFIG);
  v &= ~AUD_AUDIO_CFG_DAC_ENABLE;
  putreg32(v, BK7258_AUD_AUDIO_CONFIG);
  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);
}

uintptr_t bk7258_dac_fifo_addr(void)
{
  return (uintptr_t)BK7258_AUD_DAC_FPORT;
}

void bk7258_dac_fifo_write(uint32_t pcm_word)
{
  putreg32(pcm_word, BK7258_AUD_DAC_FPORT);
}

void bk7258_dac_reset_data_path(void)
{
  irqstate_t flags;

  /* soft_reset is active-low (1 = reset released, 0 = reset held), so a
   * data-path reset is a 0 -> 1 pulse that ends back in the operating state.
   * Never write clk_gate (bit1) — AVDK leaves it at its working default (0).
   */
  flags = spin_lock_irqsave(&g_bk7258_aud_lock);
  putreg32(getreg32(BK7258_AUD_CLK_CONTROL) & ~AUD_CLK_CTRL_SOFT_RESET,
           BK7258_AUD_CLK_CONTROL);
  up_udelay(10);
  putreg32(getreg32(BK7258_AUD_CLK_CONTROL) | AUD_CLK_CTRL_SOFT_RESET,
           BK7258_AUD_CLK_CONTROL);
  g_bk7258_dac_configured = false;
  spin_unlock_irqrestore(&g_bk7258_aud_lock, flags);
}

bool bk7258_dac_fifo_needs_data(void)
{
  const uint32_t st = getreg32(BK7258_AUD_FIFO_STATUS);

  /* Return true when either channel drops below the read-threshold; the
   * caller should replenish enough data to bring both above threshold.
   */

  return (st & (AUD_FIFO_ST_DACL_NEAR_EMPTY
                | AUD_FIFO_ST_DACR_NEAR_EMPTY)) != 0u;
}

void bk7258_dac_deinit(void)
{
  uint32_t v;

  bk7258_dac_stop();
  (void)bk7258_dac_mute(true);

  /* Reverse the analog power-up sequence.  Keep the ana_reg defaults in
   * place so a future bk7258_dac_configure() does not have to re-run the
   * whole subsystem init.
   */

  (void)bk7258_ana_reg_bit(BK7258_ANA_REG20, ANA_REG20_DACLEN_BIT, 0x1u, 0);
  (void)bk7258_ana_reg_bit(BK7258_ANA_REG21, ANA_REG21_ENIDACL_BIT, 0x1u, 0);
  (void)bk7258_ana_reg_bit(BK7258_ANA_REG20, ANA_REG20_LENDCOC_BIT, 0x1u, 0);
  (void)bk7258_ana_reg_bit(BK7258_ANA_REG20, ANA_REG20_DACDRVEN_BIT, 0x1u, 0);

  /* Restore the vendor deinit clock baseline: select XTAL in both the SYS
   * mux and AUD block, clear the APLL-only DMIC divider, then power APLL
   * down.  This makes the next configure independent of the previous one.
   */

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

  g_bk7258_dac_use_apll  = false;
  g_bk7258_dac_configured = false;
}

void bk7258_dac_get_stats(struct bk7258_dac_stats_s *out)
{
  if (out == NULL)
    {
      return;
    }

  out->version_id         = getreg32(BK7258_AUD_VERSION_ID);
  out->global_status      = getreg32(BK7258_AUD_GLOBAL_STATUS);
  out->audio_config       = getreg32(BK7258_AUD_AUDIO_CONFIG);
  out->fifo_config        = getreg32(BK7258_AUD_FIFO_CONFIG);
  out->fifo_status        = getreg32(BK7258_AUD_FIFO_STATUS);
  out->dac_config0        = getreg32(BK7258_AUD_DAC_CONFIG0);
  out->extend_cfg         = getreg32(BK7258_AUD_EXTEND_CFG);
  out->clk_control        = getreg32(BK7258_AUD_CLK_CONTROL);
  out->sys_clk_div_mode1  = getreg32(SYS_CLK_DIV_MODE1);
  out->sys_clk_div_mode2  = getreg32(SYS_CLK_DIV_MODE2);
  out->device_clk_enable  = getreg32(SYS_CPU_DEVICE_CLK_EN);
  out->device_mem_ctrl1   = getreg32(BK7258_SYS_DEV_MEM_CTRL1);
  out->device_mem_ctrl2   = getreg32(BK7258_SYS_DEV_MEM_CTRL2);
  out->power_sleep_wakeup = getreg32(BK7258_SYS_POWER_WAKEUP);
  out->analog_spi_state   = getreg32(BK7258_SYS_ANA_SPI_STATE);
  out->ana_reg5           = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG_APLL_PWD));
  out->ana_reg18          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG18));
  out->ana_reg19          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG19));
  out->ana_reg20          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG20));
  out->ana_reg21          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG21));
  out->ana_reg25          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG25_APLL_CFG));
  out->ana_reg26          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG26_APLL_CAL));
  out->ana_reg27          = getreg32(BK7258_SYS_ANA_REG(BK7258_ANA_REG27));
}

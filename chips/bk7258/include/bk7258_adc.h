/****************************************************************************
 * chips/bk7258/include/bk7258_adc.h
 *
 * Chip-level BK7258 internal-ADC (analog-MIC capture) HAL.  Like bk7258_dac.h
 * this is a register-level building block, not a NuttX audio device — the
 * lower-half in nuttx/drivers/audio/bk7258_audio.c composes it into
 * /dev/audio/pcm0c.
 *
 * The bring-up sequence mirrors bk_avdk_smp/.../aud_adc_driver.c
 * bk_aud_adc_init(): mic enable -> bias enables -> mic reset pulse -> digital
 * config -> sample-rate divider.  The one shared audio-block clock
 * (SYS_CLK_DIV_MODE1 cksel_aud + AUD_AUDIO_CONFIG apll_sel) is brought up by
 * bk7258_aud_subsystem_init() / the use_apll path below, exactly as the DAC.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_AUDIO_BK7258_ADC_H
#define __CHIPS_BK7258_AUDIO_BK7258_ADC_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_adc_channel_e
{
  BK7258_ADC_CHL_L  = 0,  /* MIC1 (left analog input).   */
  BK7258_ADC_CHL_R  = 1,  /* MIC2 (right analog input).  */
  BK7258_ADC_CHL_LR = 2,  /* Dual-MIC.                   */
};

struct bk7258_adc_cfg_s
{
  uint32_t                  samp_rate; /* 8000..48000 Hz.              */
  enum bk7258_adc_channel_e channel;
  uint8_t                   gain;      /* Digital gain 0..0x3F.        */
  bool                      samp_edge; /* false=rising, true=falling.  */
  bool                      use_apll;  /* true=APLL, false=26MHz XTAL. */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/* Configure the ADC block for capture.  Must be called after (or instead of,
 * it self-invokes) bk7258_aud_subsystem_init().  Leaves the ADC in a "primed
 * but disabled" state; bk7258_adc_start() actually clocks the capture path.
 *
 * On BK7258 the only wired analog-MIC enable is ana_reg19[28] (micen); AVDK's
 * mic2_en / aud_en are documented no-ops, so `channel` does not open a second
 * analog path — it is retained for API symmetry with the DAC HAL.
 */

int bk7258_adc_configure(const struct bk7258_adc_cfg_s *cfg);

/* Update the sample rate on an already-configured ADC. */

int bk7258_adc_set_sample_rate(uint32_t samp_rate);

/* Digital gain, 0..0x3F, clamped. */

int bk7258_adc_set_gain(uint8_t gain);

/* Enable / disable the ADC clock and data path: AVDK's adc_enable(1) +
 * line_enable(1) together (AUD_AUDIO_CFG_ADC_ENABLE + LINE_ENABLE).
 */

void bk7258_adc_start(void);
void bk7258_adc_stop(void);

/* FIFO plumbing.  fifo_addr() returns the physical address of AUD_ADC_FPORT;
 * fifo_read() pops one 32-bit word (used for polled bring-up / diagnostics).
 */

uintptr_t bk7258_adc_fifo_addr(void);
uint32_t bk7258_adc_fifo_read(void);

/* ADC-left FIFO write watermark (near-full level), 0..0x1F. */

void bk7258_adc_set_adcl_wr_threshold(uint32_t value);

/* Non-zero if the ADC-left FIFO is currently empty.  This is the clock
 * detector: after bk7258_adc_start() the capture clock fills the FIFO, so the
 * empty flag dropping proves the audio functional clock is actually running —
 * independently of the DAC write path under investigation.
 */

bool bk7258_adc_fifo_empty(void);

/* Tear the ADC block back down: stop, drop mic/bias enables, restore digital
 * defaults.  Audio subsystem clock/power stays up.
 */

void bk7258_adc_deinit(void);

/* Built-in loopback test controls (AUD_FIFO_CFG loop bits).  Diagnostics only.
 *
 *   loop_adc2dac: ADC capture fed straight back into the DAC — exercises the
 *                 ADC clock/FIFO without a host read loop.
 *   loop_ton2dac: internal DTMF tone generator fed into the DAC — exercises
 *                 the DAC write path with no host writes at all; if audio
 *                 comes out, the DAC FIFO/analog path is alive and the fault
 *                 is upstream in how we feed DAC_FPORT.
 */

void bk7258_adc_loop_adc2dac(bool enable);
void bk7258_aud_loop_ton2dac(bool enable);

/* Diagnostic snapshot of the audio-block registers relevant for capture. */

struct bk7258_adc_stats_s
{
  uint32_t audio_config;         /* AUD_AUDIO_CONFIG (0x30)   */
  uint32_t adc_config0;          /* AUD_ADC_CONFIG0  (0x04)   */
  uint32_t fifo_config;          /* AUD_FIFO_CONFIG  (0x0A)   */
  uint32_t fifo_status;          /* AUD_FIFO_STATUS  (0x0E)   */
  uint32_t extend_cfg;           /* AUD_EXTEND_CFG   (0x18)   */
  uint32_t clk_control;          /* AUD_CLK_CONTROL  (0x02)   */
  uint32_t sys_clk_div_mode1;    /* SYS CPU_CLK_DIV_MODE1     */
  uint32_t sys_clk_div_mode2;    /* SYS CPU_CLK_DIV_MODE2     */
  uint32_t device_clk_enable;    /* SYS CPU_DEVICE_CLK_ENABLE */
  uint32_t ana_reg18;            /* audbias/adcbias/micbias   */
  uint32_t ana_reg19;            /* micen/mic-rst/micgain     */
};

void bk7258_adc_get_stats(struct bk7258_adc_stats_s *out);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_AUDIO_BK7258_ADC_H */

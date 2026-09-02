/****************************************************************************
 * chips/bk7258/audio/bk7258_dac.h
 *
 * Chip-level BK7258 internal-DAC HAL.  This is not a NuttX audio device; it
 * is the register-level building block that
 * nuttx/drivers/audio/bk7258_audio.c composes into a NuttX audio lower-half
 * (registered as /dev/audio/pcm0p).
 *
 * All ordering constraints, magic values and delays are documented in
 * chips/bk7258/audio/BK7258_DAC_INIT_SEQUENCE.md.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_AUDIO_BK7258_DAC_H
#define __CHIPS_BK7258_AUDIO_BK7258_DAC_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_dac_channel_e
{
  BK7258_DAC_CHL_L  = 0,
  BK7258_DAC_CHL_R  = 1,
  BK7258_DAC_CHL_LR = 2,
};

enum bk7258_dac_workmode_e
{
  BK7258_DAC_WORK_SINGLE_END = 0,  /* Single-ended output.  */
  BK7258_DAC_WORK_DIFFEN     = 1,  /* Differential output (HT6873 default).  */
};

struct bk7258_dac_cfg_s
{
  uint32_t                     samp_rate; /* 8000..48000 Hz, see spec §2.3.  */
  enum bk7258_dac_channel_e    channel;
  enum bk7258_dac_workmode_e   work_mode;
  uint8_t                      gain;      /* Digital gain 0..0x3F, 0x2D=0dB. */
  bool                         use_apll;  /* true = APLL, false = 26MHz XTAL. */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/* One-time audio subsystem bring-up.
 *
 * Verifies the CP-owned AUDP/SRAM wake handoff, then executes the AP-owned
 * portion of spec §2.1: enables the audio clock, applies the
 * ana_reg18/19/20/21/27 defaults published by AVDK and enables the audio
 * bias network.  Idempotent — a second call is a no-op.
 */

int bk7258_aud_subsystem_init(void);

/* Configure the DAC block for playback (spec §2.2 + §2.3).
 *
 * Must be called after bk7258_aud_subsystem_init().  Leaves the DAC in the
 * "primed but disabled and muted" state.  A subsequent bk7258_dac_start()
 * is required to actually clock the analog path.
 */

int bk7258_dac_configure(const struct bk7258_dac_cfg_s *cfg);

/* Update the sample rate on an already-configured DAC.  Handles the
 * APLL cal-value refresh for the 11.025 / 22.05 / 44.1 kHz family
 * (spec §2.2 step 3, §2.3 rate table).
 */

int bk7258_dac_set_sample_rate(uint32_t samp_rate);

/* Digital gain, 0..0x3F.  Values above 0x3F are clamped. */

void bk7258_dac_set_gain(uint8_t gain);

/* Master mute (spec §3).  true asserts DAC mute in ana_reg20; false clears
 * it.  Gain is unchanged.  Returns zero or the analog SPI write error.
 */

int bk7258_dac_mute(bool mute);

/* Enable / disable the DAC clock and data path (AUD_AUDIO_CFG_DAC_ENABLE).
 * Callers own PA gating and mute ramp; see spec §2.5 / §2.6.
 */

void bk7258_dac_start(void);
void bk7258_dac_stop(void);

/* GDMA plumbing.  Returns the physical address of AUD_DAC_FPORT (0x47800048)
 * which the GDMA channel writes 32-bit words into.
 */

uintptr_t bk7258_dac_fifo_addr(void);

/* CPU-fed FIFO write, used only for early bring-up smoke tests.  Production
 * playback goes through GDMA; do not call this from an interrupt or in a
 * hot loop.
 */

void bk7258_dac_fifo_write(uint32_t pcm_word);

/* Reset only the AUDP digital data path and flush its FIFOs while preserving
 * SYS clock/power and analog configuration.  Intended for isolated bring-up
 * diagnostics; production streaming must be stopped before calling it.
 */

void bk7258_dac_reset_data_path(void);

/* Non-zero if the DAC FIFO is currently signalling "near-empty".  Cheap
 * enough to poll from a smoke test; not a substitute for the GDMA IRQ
 * (see spec §2.4).
 */

bool bk7258_dac_fifo_needs_data(void);

/* Tear the DAC block back down (spec §2.6).  Called when the last stream
 * closes; audio subsystem stays powered until board shutdown.
 */

void bk7258_dac_deinit(void);

/* Diagnostic snapshot of the audio-block registers relevant for playback. */

struct bk7258_dac_stats_s
{
  uint32_t version_id;           /* AUD_VERSION_ID             */
  uint32_t global_status;        /* AUD_GLOBAL_STATUS          */
  uint32_t audio_config;         /* AUD_AUDIO_CONFIG (0x30)    */
  uint32_t fifo_config;          /* AUD_FIFO_CONFIG  (0x0A)    */
  uint32_t fifo_status;          /* AUD_FIFO_STATUS  (0x0E)    */
  uint32_t dac_config0;          /* AUD_DAC_CONFIG0  (0x07)    */
  uint32_t extend_cfg;           /* AUD_EXTEND_CFG   (0x18)    */
  uint32_t clk_control;          /* AUD_CLK_CONTROL  (0x02)    */
  uint32_t sys_clk_div_mode1;    /* SYS CPU_CLK_DIV_MODE1      */
  uint32_t sys_clk_div_mode2;    /* SYS CPU_CLK_DIV_MODE2      */
  uint32_t device_clk_enable;    /* SYS CPU_DEVICE_CLK_ENABLE  */
  uint32_t device_mem_ctrl1;     /* SYS SRAM shutdown controls */
  uint32_t device_mem_ctrl2;     /* SYS SRAM deep-sleep ctrl   */
  uint32_t power_sleep_wakeup;   /* SYS POWER; bit6=0: AUDP on */
  uint32_t analog_spi_state;     /* SYS analog SPI busy bits   */
  uint32_t ana_reg5;             /* APLL power-down bit13      */
  uint32_t ana_reg18;
  uint32_t ana_reg19;
  uint32_t ana_reg20;
  uint32_t ana_reg21;
  uint32_t ana_reg25;            /* APLL config/trigger        */
  uint32_t ana_reg26;            /* APLL calibration           */
  uint32_t ana_reg27;
};

void bk7258_dac_get_stats(struct bk7258_dac_stats_s *out);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_AUDIO_BK7258_DAC_H */

/****************************************************************************
 * chips/bk7258/audio/bk7258_aud_reg.h
 *
 * BK7258 audio-block (0x47800000) register offsets and bit fields used by
 * the internal DAC playback path.
 *
 * Derived from bk_avdk_smp/ap/middleware/soc/bk7258_ap/hal/aud_ll_macro_def.h
 * and .../soc/aud_reg.h (Apache-2.0).  See
 * chips/bk7258/audio/BK7258_DAC_INIT_SEQUENCE.md for how each field is used.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_AUDIO_BK7258_AUD_REG_H
#define __CHIPS_BK7258_AUDIO_BK7258_AUD_REG_H

#include <nuttx/config.h>
#include <stdint.h>

#include <arch/chip/bk7258_memorymap.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Audio block base — SoC datasheet: 0x47800000, no per-domain offset on the
 * AP mapping.  All AUD_xxx offsets below are (reg_index * 4).
 */

#define BK7258_AUD_BASE               UINT32_C(0x47800000)

#define BK7258_AUD_VERSION_ID          (BK7258_AUD_BASE + (0x01u * 4u))
#define BK7258_AUD_CLK_CONTROL        (BK7258_AUD_BASE + (0x02u * 4u))
#define BK7258_AUD_GLOBAL_STATUS      (BK7258_AUD_BASE + (0x03u * 4u))
#define BK7258_AUD_DAC_CONFIG0        (BK7258_AUD_BASE + (0x07u * 4u))
#define BK7258_AUD_FIFO_CONFIG        (BK7258_AUD_BASE + (0x0Au * 4u))
#define BK7258_AUD_FIFO_STATUS        (BK7258_AUD_BASE + (0x0Eu * 4u))
#define BK7258_AUD_DAC_FPORT          (BK7258_AUD_BASE + (0x12u * 4u))
#define BK7258_AUD_EXTEND_CFG         (BK7258_AUD_BASE + (0x18u * 4u))
#define BK7258_AUD_DAC_FRACMOD        (BK7258_AUD_BASE + (0x19u * 4u))
#define BK7258_AUD_AUDIO_CONFIG       (BK7258_AUD_BASE + (0x30u * 4u))

/* AUD_CLK_CONTROL (0x02) — only the ADC soft-reset bit is documented in
 * AVDK; DAC path reuses the same bit for a full audio block reset via
 * aud_hal_set_clk_control_soft_reset().
 *
 * NOTE soft_reset is ACTIVE-LOW: 1 = reset released (operating), 0 = reset
 * held.  AVDK's init sets it to 1 and leaves it there; deinit pulses
 * 0->1->0.  The vendor comment "写1复位" is misleading — follow the code.
 */

#define AUD_CLK_CTRL_SOFT_RESET       (UINT32_C(1) << 0)
#define AUD_CLK_CTRL_CLK_GATE         (UINT32_C(1) << 1)

/* AUD_DAC_CONFIG0 (0x07) — playback signal path controls. */

#define AUD_DAC_CFG0_HPF2_BYPASS      (UINT32_C(1) << 16)
#define AUD_DAC_CFG0_HPF1_BYPASS      (UINT32_C(1) << 17)
#define AUD_DAC_CFG0_GAIN_SHIFT       18
#define AUD_DAC_CFG0_GAIN_MASK        (UINT32_C(0x3F) << AUD_DAC_CFG0_GAIN_SHIFT)
#define AUD_DAC_CFG0_CLK_INVERT       (UINT32_C(1) << 24)

/* AUD_FIFO_CONFIG (0x0A) — DAC L/R FIFO thresholds and IRQ enables. */

#define AUD_FIFO_CFG_DACR_RD_TH_SHIFT 0
#define AUD_FIFO_CFG_DACR_RD_TH_MASK  (UINT32_C(0x1F) << AUD_FIFO_CFG_DACR_RD_TH_SHIFT)
#define AUD_FIFO_CFG_DACL_RD_TH_SHIFT 5
#define AUD_FIFO_CFG_DACL_RD_TH_MASK  (UINT32_C(0x1F) << AUD_FIFO_CFG_DACL_RD_TH_SHIFT)
#define AUD_FIFO_CFG_DACR_INT_EN      (UINT32_C(1) << 20)
#define AUD_FIFO_CFG_DACL_INT_EN      (UINT32_C(1) << 21)

/* AUD_FIFO_STATUS (0x0E) — read-only status bits observed by AVDK. */

#define AUD_FIFO_ST_DACR_NEAR_FULL    (UINT32_C(1) << 0)
#define AUD_FIFO_ST_DACL_NEAR_FULL    (UINT32_C(1) << 1)
#define AUD_FIFO_ST_DACR_NEAR_EMPTY   (UINT32_C(1) << 4)
#define AUD_FIFO_ST_DACL_NEAR_EMPTY   (UINT32_C(1) << 5)
#define AUD_FIFO_ST_DACR_EMPTY        (UINT32_C(1) << 12)
#define AUD_FIFO_ST_DACL_EMPTY        (UINT32_C(1) << 13)

/* AUD_EXTEND_CFG (0x18). */

#define AUD_EXT_CFG_DAC_FRACMOD_MAN   (UINT32_C(1) << 0)
#define AUD_EXT_CFG_FILT_ENABLE       (UINT32_C(1) << 2)

/* AUD_AUDIO_CONFIG (0x30). */

#define AUD_AUDIO_CFG_DAC_ENABLE      (UINT32_C(1) << 2)
#define AUD_AUDIO_CFG_ADC_ENABLE      (UINT32_C(1) << 3)
#define AUD_AUDIO_CFG_SAMP_RATE_SHIFT 6
#define AUD_AUDIO_CFG_SAMP_RATE_MASK  (UINT32_C(0x3) << AUD_AUDIO_CFG_SAMP_RATE_SHIFT)
#define AUD_AUDIO_CFG_APLL_SEL        (UINT32_C(1) << 8)

#define AUD_SAMP_RATE_8K              0u
#define AUD_SAMP_RATE_16K             1u
#define AUD_SAMP_RATE_44_1K           2u
#define AUD_SAMP_RATE_48K             3u

/* DAC fractional-divider constants taken verbatim from
 * bk_avdk_smp/ap/middleware/driver/audio/aud/aud_dac_driver.c:44..47.
 * Silicon-calibrated; do NOT modify.
 */

#define BK7258_AUD_CONST_DIV_16K      UINT32_C(0x06590000)
#define BK7258_AUD_CONST_DIV_32K      UINT32_C(0x01964000)
#define BK7258_AUD_CONST_DIV_44_1K    UINT32_C(0x049B2368)
#define BK7258_AUD_CONST_DIV_48K      UINT32_C(0x043B5554)

/* Sys-ctrl analog reg SPI window.  Every write to ana_regN (N in 0..0x2A,
 * mapped at SOC_SYS_REG_BASE + ((0x40 + N) << 2)) must be followed by
 * polling the SPI-state bit N in this register until it clears.
 */

#define BK7258_SYS_ANALOG_REG_BASE    (BK7258_SYS_BASE + UINT32_C(0x40) * 4u)
#define BK7258_SYS_ANA_SPI_STATE      (BK7258_SYS_BASE + UINT32_C(0x3A) * 4u)

#define BK7258_SYS_ANA_REG(n)         (BK7258_SYS_ANALOG_REG_BASE + ((n) * 4u))

/* APLL clock configuration constants (silicon-calibrated). */

#define BK7258_APLL_CAL_VAL_48K       UINT32_C(0x8973CA6F)  /* 8/16/32/48 */
#define BK7258_APLL_CAL_VAL_44_1K     UINT32_C(0x88AF2EC9)  /* 11/22/44.1 */
#define BK7258_APLL_CONFIG_VAL        UINT32_C(0xC2A0AE86)

/****************************************************************************
 * ADC / capture path (dual analog-MIC).
 *
 * Derived from bk_avdk_smp/.../aud_adc_driver.c and the aud_hal.h register
 * map.  The analog MIC enables live in ana_reg18/19 (bit positions from
 * bk7258_ap sys_struct.h); the digital ADC controls are in AUD_ADC_CONFIG0.
 ****************************************************************************/

#define BK7258_AUD_ADC_CONFIG0        (BK7258_AUD_BASE + (0x04u * 4u)) /* 0x47800010 */
#define BK7258_AUD_ADC_FPORT          (BK7258_AUD_BASE + (0x11u * 4u)) /* 0x47800044 */
#define BK7258_AUD_ADC_FRACMOD        (BK7258_AUD_BASE + (0x1Au * 4u)) /* 0x47800068 */

/* AUD_EXTEND_CFG (0x18) ADC-side bit — manual fractional-divider select.
 * AVDK clears this for the standard 8/16/44.1/48k rates (auto divider).
 */

#define AUD_EXT_CFG_ADC_FRACMOD_MAN   (UINT32_C(1) << 1)

/* AUD_ADC_CONFIG0 (0x04) — capture signal-path controls. */

#define AUD_ADC_CFG0_HPF2_BYPASS      (UINT32_C(1) << 16)
#define AUD_ADC_CFG0_HPF1_BYPASS      (UINT32_C(1) << 17)
#define AUD_ADC_CFG0_GAIN_SHIFT       18
#define AUD_ADC_CFG0_GAIN_MASK        (UINT32_C(0x3F) << AUD_ADC_CFG0_GAIN_SHIFT)
#define AUD_ADC_CFG0_SAMP_EDGE        (UINT32_C(1) << 24)  /* 0=rising, 1=falling */

/* AUD_AUDIO_CONFIG (0x30) — capture-side bits.  samp_rate_ADC is [1:0], a
 * *different* field from samp_rate_DAC [7:6].
 */

#define AUD_AUDIO_CFG_LINE_ENABLE            (UINT32_C(1) << 5)
#define AUD_AUDIO_CFG_SAMP_RATE_ADC_SHIFT    0
#define AUD_AUDIO_CFG_SAMP_RATE_ADC_MASK     \
  (UINT32_C(0x3) << AUD_AUDIO_CFG_SAMP_RATE_ADC_SHIFT)

/* AUD_FIFO_CONFIG (0x0A) — ADC threshold/IRQ + built-in loopback tests. */

#define AUD_FIFO_CFG_ADCL_WR_TH_SHIFT        15
#define AUD_FIFO_CFG_ADCL_WR_TH_MASK         \
  (UINT32_C(0x1F) << AUD_FIFO_CFG_ADCL_WR_TH_SHIFT)
#define AUD_FIFO_CFG_ADCL_INT_EN             (UINT32_C(1) << 23)
#define AUD_FIFO_CFG_LOOP_TON2DAC            (UINT32_C(1) << 24)
#define AUD_FIFO_CFG_LOOP_ADC2DAC            (UINT32_C(1) << 25)

/* AUD_FIFO_STATUS (0x0E) — ADC-left-channel FIFO status. */

#define AUD_FIFO_ST_ADCL_NEAR_FULL           (UINT32_C(1) << 2)
#define AUD_FIFO_ST_ADCL_NEAR_EMPTY          (UINT32_C(1) << 6)
#define AUD_FIFO_ST_ADCL_EMPTY               (UINT32_C(1) << 14)

/* Analog MIC/ADC enables (ana_reg18 / ana_reg19 bit positions). */

#define ANA_REG18_ENADCBias_BIT              4
#define ANA_REG18_ENMICBias_BIT              5

#define ANA_REG19_MICGAIN_SHIFT              15   /* 4-bit field          */
#define ANA_REG19_MICGAIN_MASK               0xFu
#define ANA_REG19_MICEN_BIT                  28   /* MIC1 preamp enable   */
#define ANA_REG19_MIC_RST_BIT                29   /* MIC reset, act-high  */

/* ADC fractional-divider constants are the same silicon table the DAC uses
 * (aud_adc_driver.c shares the CONST_DIV_* words); reuse BK7258_AUD_CONST_DIV_*.
 */

#endif /* __CHIPS_BK7258_AUDIO_BK7258_AUD_REG_H */

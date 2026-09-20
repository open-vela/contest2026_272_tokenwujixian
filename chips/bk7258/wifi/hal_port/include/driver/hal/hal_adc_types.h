/*
 * chips/bk7258/wifi/hal_port/include/driver/hal/hal_adc_types.h
 *
 * ADC types used by the vendored bk_phy_adapter.c, whose bk_cal_saradc_start()
 * (line 418) builds an adc_config_t for RF calibration.
 *
 * Previously an empty compatibility header here. The stated reason -- that
 * bk_phy_adapter.h / bk_rf_adapter.h reference none of these types -- was
 * correct but insufficient: the check only covered those two headers, while the
 * .c file behind them does use adc_config_t and the enum constants. Third
 * instance of the same verification gap; the lesson is to grep the compiled .c
 * set, not just the header that pulls the include in.
 *
 * Values are upstream's. Deliberately does NOT include driver/hal/hal_gpio_types.h
 * (upstream does): that would collide with gpio_id_t / gpio_dev_t in our own
 * gpio_driver.h, and nothing here needs GPIO types.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_HAL_HAL_ADC_TYPES_H
#define __BK7258_WIFI_GLUE_DRIVER_HAL_HAL_ADC_TYPES_H

#include <stdint.h>

/* Authority BK7258 driver/hal/hal_adc_types.h defines this unconditionally.
 * It controls the public-to-hardware saturation mapping in adc_hal.c. */

#define ADC_ACURACY_12_BIT 1
#define ADC_SRC_DCO_CLK    120000000u
#define ADC_SRC_DPLL_CLK   240000000u
#define ADC_SRC_26M_CLK    26000000u
#define ADC_SRC_32M_CLK    32000000u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  ADC_SCLK_DCO = 0,
  ADC_SCLK_XTAL_26M,
  ADC_SCLK_DPLL,
  ADC_SCLK_32M,
  ADC_SCLK_NONE
} adc_src_clk_t;

typedef enum
{
  ADC_SLEEP_MODE = 0,
  ADC_SINGLE_STEP_MODE,
  ADC_SOFTWARE_CONTRL_MODE,
  ADC_CONTINUOUS_MODE,
  ADC_NONE_MODE
} adc_mode_t;

typedef enum
{
  ADC_0 = 0,
  ADC_1,
  ADC_2,
  ADC_3,
  ADC_4,
  ADC_5,
  ADC_6,
  ADC_7,
  ADC_8,
  ADC_9,
  ADC_10,
  ADC_11,
  ADC_12,
  ADC_13,
  ADC_14,
  ADC_15,
  ADC_MAX
} adc_chan_t;

/* Upstream ships two variants of this enum, gated on ADC_ACURACY_10_BIT: the
 * 10-bit one stops at MODE_1. bk_phy_adapter.c:429 uses ADC_SATURATE_MODE_3, so
 * the wider (non-10-bit) variant is the one in force -- reproduced here. If a
 * 10-bit ADC configuration ever becomes relevant, this enum and that call site
 * have to be revisited together.
 */

typedef enum
{
  ADC_SATURATE_MODE_NONE = 0,
  ADC_SATURATE_MODE_0,
  ADC_SATURATE_MODE_1,
  ADC_SATURATE_MODE_2,
  ADC_SATURATE_MODE_3
} adc_saturate_mode_t;

typedef enum
{
  ADC_VOL_DIV_NONE = 0,
  ADC_VOL_DIV_1,
  ADC_VOL_DIV_2,
  ADC_VOL_DIV_3,
  ADC_VOL_DIV_4,
  ADC_VOL_DIV_5,
  ADC_VOL_DIV_7
} adc_vol_div_t;

/* Field order is upstream's. It stays glue-internal (the config is filled by
 * bk_phy_adapter.c and consumed by our own shim, not by the prebuilt), but
 * keeping the order identical avoids surprises if that ever changes.
 */

typedef struct
{
  uint32_t            clk;
  uint32_t            sample_rate;
  uint32_t            adc_filter;
  uint32_t            steady_ctrl;
  adc_mode_t          adc_mode;
  adc_src_clk_t       src_clk;
  adc_chan_t          chan;
  adc_saturate_mode_t saturate_mode;
  uint32_t            is_open;
  uint16_t           *output_buf;
  int32_t             output_buf_len;
  uint16_t            is_hw_using_cali_result;
  adc_vol_div_t       vol_div;
} adc_config_t;

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_HAL_HAL_ADC_TYPES_H */

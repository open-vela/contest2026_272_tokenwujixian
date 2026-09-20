/*
 * chips/bk7258/wifi/hal_port/include/temp_detect.h
 *
 * Temperature-detection tuning constants. bk_phy_adapter.c puts these in the PHY
 * value table, so libbk_phy.a uses them to convert raw ADC counts into
 * temperature and to decide when to re-calibrate.
 *
 * ==== SoC branch, and why it is easy to get wrong ====
 *
 * Upstream defines most of these three or four times over
 * (components/temp_detect/temp_detect.h:27-100), nested two levels deep: first
 * on SoC family, then on CONFIG_SDMADC_TEMP. BK7258 is BK7236XX family, and
 * CONFIG_SDMADC_TEMP appears in neither the prebuilt baseline sdkconfig.h nor the
 * genie project config, so the values below come from the
 * `BK7236XX && !SDMADC_TEMP` path.
 *
 * The difference is not cosmetic: a naive grep lands on the SDMADC variant first
 * and would give ADC_TMEP_LSB_PER_10DEGREE 252 instead of 46 (5.5x error in the
 * counts-per-degree slope) and ADC_TEMP_VAL_MIN 3192 instead of 10. Both compile
 * and link, then silently mis-scale every temperature reading -- which feeds RF
 * power compensation.
 */

#ifndef __BK7258_WIFI_GLUE_TEMP_DETECT_H
#define __BK7258_WIFI_GLUE_TEMP_DETECT_H

#include "driver/hal/hal_adc_types.h"

/* ADC channel and saturate mode: channel 7 is the `#else` default; saturate mode
 * is the BK7236XX-specific ADC_SATURATE_MODE_2 (not MODE_3, which is BK7256XX
 * and the default branch).
 */

#define ADC_TEMP_SENSOR_CHANNEL       7
#define ADC_TEMP_SATURATE_MODE        ADC_SATURATE_MODE_2

/* BK7236XX && !CONFIG_SDMADC_TEMP. */

#define ADC_TEMP_BUFFER_SIZE          (5 + 5)   /* +5 samples skipped */
#define ADC_TMEP_LSB_PER_10DEGREE     (46)      /* 30 if ana_reg5_adc_div = 1/7 */
#define ADC_TEMP_VAL_MIN              (10)
#define ADC_TEMP_VAL_MAX              (1365)    /* for ana_reg5_adc_div = 1/3 */

/* SoC-independent. */

#define ADC_TMEP_DIST_INTIAL_VAL      (0)
#define ADC_TMEP_10DEGREE_PER_DBPWR   (1)
#define ADC_XTAL_DIST_INTIAL_VAL      (70)

#define ADC_TMEP_DETECT_INTERVAL_INIT   (1)   /* seconds */
#define ADC_TMEP_DETECT_INTERVAL        (15)
#define ADC_TMEP_DETECT_INTERVAL_CHANGE (30)
#define ADC_TMEP_XTAL_INIT              (60)

#define TEMP_DETEC_ADC_CLK            203125
#define TEMP_DETEC_ADC_SAMPLE_RATE    0
#define TEMP_DETEC_ADC_STEADY_CTRL    7

#endif /* __BK7258_WIFI_GLUE_TEMP_DETECT_H */

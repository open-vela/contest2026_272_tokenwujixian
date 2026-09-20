/*
 * chips/bk7258/wifi/hal_port/include/adc_hal.h
 *
 * Upstream's home for SARADC_AUTOTEST (middleware/soc/common/hal/include/
 * adc_hal.h:94), which is 0 in the shipping configuration -- the SARADC
 * self-test path is off there too.
 *
 * bk_phy_adapter.c includes this header and references nothing else from it
 * (checked over both its macros and its function declarations), so only that one
 * macro is reproduced. Its value keeps the `#if SARADC_AUTOTEST` block at
 * bk_phy_adapter.c:454-472 dead, which is why bk_saradc_set_config() never needs
 * an implementation.
 *
 * Defined here rather than in bk_saradc.h so there is a single definition at the
 * path upstream uses; bk_saradc.h forwards to this file.
 */

#ifndef __BK7258_WIFI_GLUE_ADC_HAL_H
#define __BK7258_WIFI_GLUE_ADC_HAL_H

#ifndef SARADC_AUTOTEST
#  define SARADC_AUTOTEST 0
#endif

#endif /* __BK7258_WIFI_GLUE_ADC_HAL_H */

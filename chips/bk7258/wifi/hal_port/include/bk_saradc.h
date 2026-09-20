/*
 * chips/bk7258/wifi/hal_port/include/bk_saradc.h
 *
 * SARADC surface used by the vendored bk_phy_adapter.c: saradc_calculate()
 * converts a raw count to a voltage for the PHY function table, and
 * g_saradc_flag is a shared state byte the adapter writes
 * (bk_phy_adapter.c:53-56).
 *
 * SARADC_AUTOTEST lives in adc_hal.h, where upstream defines it (:94), and is
 * forwarded rather than duplicated. Its value 0 keeps the
 * `#if SARADC_AUTOTEST` block at bk_phy_adapter.c:454-472 dead, which is why
 * bk_saradc_set_config() needs no implementation.
 *
 * saradc_calculate() returns float -- upstream's signature, and worth noting
 * because it means the vendor value/function tables carry a float return across
 * the ABI. Implemented in hal_port/analog_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_BK_SARADC_H
#define __BK7258_WIFI_GLUE_BK_SARADC_H

#include <common/bk_typedef.h>

#include "adc_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern UINT8 g_saradc_flag;

float saradc_calculate(UINT16 adc_val);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BK_SARADC_H */

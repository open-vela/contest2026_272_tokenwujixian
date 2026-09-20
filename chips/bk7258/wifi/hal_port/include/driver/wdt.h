/*
 * chips/bk7258/wifi/hal_port/include/driver/wdt.h
 *
 * Watchdog stop, registered by the vendored bk_phy_adapter.c into the PHY
 * function table: upstream stops the watchdog around long calibration steps that
 * would otherwise trip it.
 *
 * Previously an empty compatibility header here, on the grounds that
 * bk_phy_adapter.c "references no watchdog symbol". That was the same flawed
 * check as with driver/efuse.h and friends -- it matched only `symbol(` and so
 * missed the capability-table entry, which is a link-time reference. Sixth and
 * final instance of that mistake in this layer.
 *
 * Signature is upstream's (include/driver/wdt.h:88, minus its .itcm_sec_code
 * placement, which is an Armino linker-section concern). Implemented in
 * hal_port/flash_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_WDT_H
#define __BK7258_WIFI_GLUE_DRIVER_WDT_H

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_wdt_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_WDT_H */

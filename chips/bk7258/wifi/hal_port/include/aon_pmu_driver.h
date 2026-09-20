/*
 * chips/bk7258/wifi/hal_port/include/aon_pmu_driver.h
 *
 * AON PMU calibration accessors used by the vendored bk_phy_adapter.c, which
 * wraps them into the PHY capability table so libbk_phy.a can call them.
 *
 * The full Armino BK7258 authority driver now supplies these calibration
 * accessors and its companion PMU APIs; this legacy path is declaration-only.
 */

#ifndef __BK7258_WIFI_GLUE_AON_PMU_DRIVER_H
#define __BK7258_WIFI_GLUE_AON_PMU_DRIVER_H

/* Retain the legacy include path while forwarding declarations to the
 * authority driver API. */
#include "../../../aon_pmu/armino/middleware/driver/pmu/aon_pmu_driver.h"

#endif /* __BK7258_WIFI_GLUE_AON_PMU_DRIVER_H */

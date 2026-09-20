/*
 * chips/bk7258/wifi/hal_port/include/aon_pmu_hal.h
 *
 * AON PMU register accessor used by the vendored bk_phy_adapter.c:165
 * (`aon_pmu_hal_get_reg0x7c()` -> `aon_pmu_hal_reg_get(PMU_REG0x7c)`), which is
 * registered into the PHY capability table, so libbk_phy.a can read it.
 *
 * The full Armino BK7258 authority HAL now provides the live implementation,
 * its PMU enum/address map, and the generated AON LL register access.
 */

#ifndef __BK7258_WIFI_GLUE_AON_PMU_HAL_H
#define __BK7258_WIFI_GLUE_AON_PMU_HAL_H

/* Retain the legacy include path, but use the full authority API and its
 * authority-owned PMU enum/address map. */
#include "../../../aon_pmu/armino/middleware/soc/common/hal/include/aon_pmu_hal.h"

#endif /* __BK7258_WIFI_GLUE_AON_PMU_HAL_H */

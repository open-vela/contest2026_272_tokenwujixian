/*
 * chips/bk7258/wifi/hal_port/include/bk_icu.h
 *
 * ICU command codes and clock-power bits used by the vendored bk_phy_adapter.c
 * (CMD_TL410_CLK_PWR_UP goes into the PHY value table, PWD_BLE_CLK_BIT is used
 * when gating the BLE clock for coexistence).
 *
 * CMD_TL410_CLK_PWR_UP is an enum member anchored at ICU_CMD_MAGIC (0xE220000),
 * so its value comes from member order, not from its own line. The enum was
 * walked to derive 0x0E220016; written as an explicit constant because a trimmed
 * enum would renumber silently.
 */

#ifndef __BK7258_WIFI_GLUE_BK_ICU_H
#define __BK7258_WIFI_GLUE_BK_ICU_H

#include <common/bk_typedef.h>

#define ICU_CMD_MAGIC               (0xE220000)

#define CMD_ICU_CLKGATING_DISABLE   (0x0E220001)
#define CMD_TL410_CLK_PWR_UP        (0x0E220016)

#define PWD_BLE_CLK_BIT             (1 << 1)

#endif /* __BK7258_WIFI_GLUE_BK_ICU_H */

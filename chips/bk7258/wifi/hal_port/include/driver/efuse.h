/*
 * chips/bk7258/wifi/hal_port/include/driver/efuse.h
 *
 * eFuse byte access. bk_phy_adapter.c registers bk_efuse_read_byte into the PHY
 * capability table (line 666), so libbk_phy.a reads fused calibration/trim data
 * through it.
 *
 * Previously an empty header here -- corrected: our "included but unused" check
 * matched only `symbol(`, so it missed capability-table entries written as
 * `._bk_efuse_read_byte = bk_efuse_read_byte,`. Those resolve at link time.
 *
 * Implementation in hal_port/flash_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_EFUSE_H
#define __BK7258_WIFI_GLUE_DRIVER_EFUSE_H

#include <stdint.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_efuse_read_byte(uint8_t addr, uint8_t *data);
bk_err_t bk_efuse_write_byte(uint8_t addr, uint8_t data);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_EFUSE_H */

/*
 * chips/bk7258/wifi/hal_port/include/driver/flash.h
 *
 * Flash access registered by the vendored bk_phy_adapter.c into the PHY function
 * table, so libbk_phy.a can read -- and rewrite -- RF calibration data:
 * bk_flash_read_bytes (line 592), plus erase/write/protect for the write-back
 * path.
 *
 * Implementations are in hal_port/flash_shim.c and all fail: this team repo's flash
 * layout has no RF calibration partition yet (see that file). The write and erase
 * paths matter especially -- a stub that pretended to succeed would let the PHY
 * believe calibration was persisted, and one that actually wrote somewhere
 * plausible could corrupt a real partition.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_FLASH_H
#define __BK7258_WIFI_GLUE_DRIVER_FLASH_H

#include <stdint.h>

#include <common/bk_err.h>

#include "driver/flash_types.h"
#include "driver/flash_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_flash_read_bytes(uint32_t address, uint8_t *user_buf,
                             uint32_t size);
bk_err_t bk_flash_write_bytes(uint32_t address, const uint8_t *user_buf,
                              uint32_t size);
bk_err_t bk_flash_erase_sector(uint32_t address);
bk_err_t bk_flash_set_protect_type(flash_protect_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_FLASH_H */

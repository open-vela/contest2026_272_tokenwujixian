/*
 * chips/bk7258/wifi/hal_port/include/driver/flash_types.h
 *
 * Flash write-protect modes. bk_phy_adapter.c wraps
 * bk_flash_set_protect_type(FLASH_PROTECT_NONE) and
 * bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK) into the PHY function
 * table, because writing RF calibration data back to flash needs protection
 * lifted first.
 *
 * Values are upstream's (include/driver/flash_types.h:36-43); the enum is
 * reproduced whole since the members are positional
 * (FLASH_UNPROTECT_LAST_BLOCK is 3 only because two modes precede it).
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_FLASH_TYPES_H
#define __BK7258_WIFI_GLUE_DRIVER_FLASH_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  FLASH_PROTECT_NONE = 0,
  FLASH_PROTECT_ALL,
  FLASH_PROTECT_HALF,
  FLASH_UNPROTECT_LAST_BLOCK
} flash_protect_type_t;

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_FLASH_TYPES_H */

/****************************************************************************
 * chips/bk7258/include/bk7258_flash.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_FLASH_H
#define __VENDOR_BEKEN_CHIP_BK7258_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define BK7258_FLASH_SIZE_BYTES UINT32_C(0x00800000)

int bk7258_flash_initialize(void);
int bk7258_flash_read_id(uint32_t *id);
int bk7258_flash_read(uint32_t offset, void *buffer, size_t length);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_FLASH_H */

/****************************************************************************
 * chips/bk7258/include/bk7258_psram.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_PSRAM_H
#define __VENDOR_BEKEN_CHIP_BK7258_PSRAM_H

#include <stddef.h>
#include <stdint.h>

/* Bring up the fitted PSRAM device and return its usable mapped capacity.
 * The caller owns deciding whether a successful region is published to an
 * allocator. A negative errno value means no PSRAM memory is usable.
 */

int bk7258_psram_initialize(size_t *size, uint16_t *device_id);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_PSRAM_H */

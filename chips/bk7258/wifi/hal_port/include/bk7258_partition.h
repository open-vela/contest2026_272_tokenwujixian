/****************************************************************************
 * board/bk7258-devkit/include/bk7258_partition.h
 ****************************************************************************/

#ifndef __BOARD_BK7258_DEVKIT_BK7258_PARTITION_H
#define __BOARD_BK7258_DEVKIT_BK7258_PARTITION_H

#include <stddef.h>
#include <stdint.h>

/* Armino projects/app BK7258 data-partition contract. These are board Flash
 * regions, not SoC MMIO addresses and not part of the CP/AP image metadata. */

#define BK7258_FLASH_SIZE                 UINT32_C(0x00800000)
#define BK7258_PARTITION_SYS_RF           3u
#define BK7258_PARTITION_SYS_NET          4u
#define BK7258_PART_SYS_RF_OFFSET        UINT32_C(0x007fe000)
#define BK7258_PART_SYS_RF_SIZE          UINT32_C(0x00001000)
#define BK7258_PART_SYS_NET_OFFSET       UINT32_C(0x007ff000)
#define BK7258_PART_SYS_NET_SIZE         UINT32_C(0x00001000)

#define BK7258_SYS_NET_MAC_OFFSET        UINT32_C(0x00000000)
#define BK7258_SYS_NET_FAST_CONNECT_OFFSET UINT32_C(0x00000006)
#define BK7258_SYS_RF_MAC_BACKUP_OFFSET  UINT32_C(0x00000e00)
#define BK7258_SYS_RF_MAC_BACKUP_SIZE    UINT32_C(0x00000200)

int bk7258_partition_get_info(unsigned int partition, uint32_t *start,
                              uint32_t *length);
int bk7258_partition_read(unsigned int partition, void *buffer,
                          uint32_t offset, size_t length);

#endif /* __BOARD_BK7258_DEVKIT_BK7258_PARTITION_H */

/*
 * chips/bk7258/wifi/hal_port/include/driver/flash_partition.h
 *
 * Flash partition lookup used by the vendored bk_phy_adapter.c:227, which
 * reads the RF calibration blob from BK_PARTITION_SYS_RF.
 *
 * Previously an empty compatibility header here -- that was a verification
 * mistake on our side: the check for "included but unused" matched only
 * `symbol(`, which misses capability-table entries written as
 * `._bk_flash_read_bytes = bk_flash_read_bytes,`. Those are real references
 * that fail at link time, not compile time.
 *
 * Types and partition ids are upstream's. The ids are #defines rather than
 * enum members and come from a *generated* header,
 * projects/beken_genie/build/bk7258/beken_genie/partitions/partitions_gen.h
 * (the bk_partition_t enum in upstream's own flash_partition.h is commented
 * out).
 *
 * NOT copied: the partition offsets from that generated file
 * (SYS_RF at 0x007fe000, SYS_NET at 0x007ff000). Flash layout is a product
 * decision and this team repo has its own, so hardcoding the genie numbers
 * would point RF calibration at an address that is only right by accident. The
 * lookup is therefore unimplemented on purpose -- see hal_port/flash_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_FLASH_PARTITION_H
#define __BK7258_WIFI_GLUE_DRIVER_FLASH_PARTITION_H

#include <stdint.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Partition ids, values as partitions_gen.h:41-49. */

#define BK_PARTITION_BOOTLOADER    0
#define BK_PARTITION_APPLICATION   1
#define BK_PARTITION_APPLICATION1  2
#define BK_PARTITION_SYS_RF        3
#define BK_PARTITION_SYS_NET       4
#define BK_PARTITION_OTA           5
#define BK_PARTITION_USR_CONFIG    6
#define BK_PARTITION_EASYFLASH     7
#define BK_PARTITION_EASYFLASH_AP  8

#define PAR_OPT_READ_POS   (0)
#define PAR_OPT_READ_DIS   (0x0u << PAR_OPT_READ_POS)
#define PAR_OPT_READ_EN    (0x1u << PAR_OPT_READ_POS)
#define PAR_OPT_WRITE_POS  (1)
#define PAR_OPT_WRITE_DIS  (0x0u << PAR_OPT_WRITE_POS)
#define PAR_OPT_WRITE_EN   (0x1u << PAR_OPT_WRITE_POS)

typedef uint32_t bk_partition_t;

typedef enum
{
  BK_FLASH_EMBEDDED = 0,
  BK_FLASH_SPI      = 1,
  BK_FLASH_MAX      = 2,
  BK_FLASH_NONE     = 3
} bk_flash_t;

/* Field order is upstream's: the vendored code reads ->partition_start_addr
 * and ->partition_length off this.
 */

typedef struct
{
  bk_flash_t  partition_owner;
  const char *partition_description;
  uint32_t    partition_start_addr;
  uint32_t    partition_length;
  uint32_t    partition_options;
} bk_logic_partition_t;

bk_logic_partition_t *bk_flash_partition_get_info(bk_partition_t partition);
bk_err_t bk_flash_partition_read(bk_partition_t partition, uint8_t *out_buffer,
                                 uint32_t offset, uint32_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_FLASH_PARTITION_H */

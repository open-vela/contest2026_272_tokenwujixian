/*
 * chips/bk7258/wifi/hal_port/flash_shim.c
 *
 * Flash / partition / eFuse / OTP accessors that the vendored bk_phy_adapter.c
 * registers into the PHY capability table, so libbk_phy.a reads RF calibration
 * through them.
 *
 * ==== Why these are stubs, and why they are shaped like this ====
 *
 * The board owns SYS_RF/SYS_NET at the reserved tail of the 8 MiB Flash. This
 * shim translates the vendor ABI to the board read-only API. Write, erase and
 * protection changes remain refused until a separate persistence contract is
 * defined and tested.
 *
 * The shape is forced by the caller:
 * bk_flash_partition_get_rf_firmware_info() (bk_phy_adapter.c:224-229) does
 *
 *     pt = bk_flash_partition_get_info(BK_PARTITION_SYS_RF);
 *     return pt->partition_start_addr;
 *
 * with no NULL check. So returning NULL is not "safe failure", it is a null
 * dereference during PHY init. Instead:
 *
 *   - get_info() returns the real descriptor, so PHY can locate SYS_RF;
 *   - reads are restricted to SYS_RF/SYS_NET and validated by the board API;
 *   - every write is refused outright, so calibration cannot alter the tail.
 *
 * That ordering matters: silently returning zeros would let the PHY calibrate
 * against fabricated values, which is worse than a clean failure -- it can mean
 * out-of-spec RF output.
 *
 * Temperature/voltage monitoring lives in hal_port/analog_shim.c, not here.
 *
 */

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>
#include <components/log.h>

#include <arch/chip/bk7258_flash.h>
#include <bk7258_partition.h>

#include "driver/flash.h"
#include "driver/flash_types.h"
#include "driver/flash_partition.h"
#include "driver/efuse.h"
#include "driver/otp.h"
#include "driver/wdt.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define FLASH_TAG "bk_flash"

#define FLASH_UNPORTED(_name)                                              \
  do                                                                      \
    {                                                                     \
      static bool _warned;                                                \
      if (!_warned)                                                       \
        {                                                                 \
          _warned = true;                                                 \
          BK_LOGE(FLASH_TAG,                                              \
                  "%s: no RF calibration store on BK7258 yet; "           \
                  "calibration data is NOT available\n", (_name));        \
        }                                                                 \
    }                                                                     \
  while (0)

/****************************************************************************
 * Private data
 ****************************************************************************/

/* Zero-length descriptor: non-NULL so the caller above does not fault, but
 * describing an empty region so nothing treats it as usable storage.
 */

static bk_logic_partition_t g_bk7258_rf_partition =
{
  .partition_owner       = BK_FLASH_EMBEDDED,
  .partition_description = "BK7258 SYS_RF (read-only)",
  .partition_start_addr  = BK7258_PART_SYS_RF_OFFSET,
  .partition_length      = BK7258_PART_SYS_RF_SIZE,
  .partition_options     = PAR_OPT_READ_EN | PAR_OPT_WRITE_DIS,
};

static bk_logic_partition_t g_bk7258_net_partition =
{
  .partition_owner       = BK_FLASH_EMBEDDED,
  .partition_description = "BK7258 SYS_NET (read-only)",
  .partition_start_addr  = BK7258_PART_SYS_NET_OFFSET,
  .partition_length      = BK7258_PART_SYS_NET_SIZE,
  .partition_options     = 0,
};

static bool bk7258_flash_readable_region(uint32_t address, uint32_t size)
{
  uint32_t end = address + size;

  if (end < address)
    {
      return false;
    }

  return (address >= BK7258_PART_SYS_RF_OFFSET &&
          end <= BK7258_PART_SYS_RF_OFFSET + BK7258_PART_SYS_RF_SIZE) ||
         (address >= BK7258_PART_SYS_NET_OFFSET &&
          end <= BK7258_PART_SYS_NET_OFFSET + BK7258_PART_SYS_NET_SIZE);
}

/****************************************************************************
 * Partition lookup
 ****************************************************************************/

bk_logic_partition_t *bk_flash_partition_get_info(bk_partition_t partition)
{
  if (partition == BK_PARTITION_SYS_RF)
    {
      return &g_bk7258_rf_partition;
    }

  if (partition == BK_PARTITION_SYS_NET)
    {
      return &g_bk7258_net_partition;
    }

  return NULL;
}

/****************************************************************************
 * Read paths -- the real gate. Each fails without writing the output buffer.
 ****************************************************************************/

bk_err_t bk_flash_read_bytes(uint32_t address, uint8_t *user_buf,
                             uint32_t size)
{
  if (size != 0 && (user_buf == NULL ||
                    !bk7258_flash_readable_region(address, size)))
    {
      return BK_FAIL;
    }

  if (bk7258_flash_initialize() < 0)
    {
      return BK_FAIL;
    }

  return bk7258_flash_read(address, user_buf, size) == 0 ? BK_OK : BK_FAIL;
}

bk_err_t bk_flash_partition_read(bk_partition_t partition, uint8_t *out_buffer,
                                 uint32_t offset, uint32_t buffer_len)
{
  return bk7258_partition_read(partition, out_buffer, offset, buffer_len) == 0 ?
         BK_OK : BK_FAIL;
}

/****************************************************************************
 * Write paths -- refused, not stubbed
 *
 * These exist because the PHY can write calibration results back. Writing to a
 * guessed address could corrupt a real partition (the team layout has no RF
 * region), so refusal is the only correct answer here.
 ****************************************************************************/

bk_err_t bk_flash_write_bytes(uint32_t address, const uint8_t *user_buf,
                              uint32_t size)
{
  (void)address;
  (void)user_buf;
  (void)size;
  FLASH_UNPORTED("bk_flash_write_bytes");
  return BK_FAIL;
}

bk_err_t bk_flash_erase_sector(uint32_t address)
{
  (void)address;
  FLASH_UNPORTED("bk_flash_erase_sector");
  return BK_FAIL;
}

bk_err_t bk_flash_set_protect_type(flash_protect_type_t type)
{
  /* Refused deliberately: this is the call that lifts write protection. Letting
   * it "succeed" while the write path is unported would be harmless today, but
   * it would also hide that protection state is not actually managed here.
   */

  (void)type;
  FLASH_UNPORTED("bk_flash_set_protect_type");
  return BK_FAIL;
}

/****************************************************************************
 * eFuse
 ****************************************************************************/

#if 0 /* Moved to chips/bk7258/otp/bk7258_otp.c; retained as fallback. */
bk_err_t bk_efuse_read_byte(uint8_t addr, uint8_t *data)
{
  /* eFuse holds trim/calibration bytes. Failing is the honest answer while the
   * BK7258 eFuse driver is unported; a fabricated value would feed the PHY
   * silently wrong trim data.
   */

  (void)addr;
  (void)data;
  FLASH_UNPORTED("bk_efuse_read_byte");
  return BK_FAIL;
}

bk_err_t bk_efuse_write_byte(uint8_t addr, uint8_t data)
{
  /* Refused outright: eFuse programming is irreversible. */

  (void)addr;
  (void)data;
  return BK_FAIL;
}

/****************************************************************************
 * OTP
 *
 * Item ids come from a generated header tied to the genie OTP layout (see
 * driver/otp.h). Reading with an id derived from someone else's layout would
 * return plausible-looking but wrong bytes, so these fail until this repo pins
 * its own OTP map.
 ****************************************************************************/

bk_err_t bk_otp_apb_read(otp1_id_t item, uint8_t *buf, uint32_t size)
{
  (void)item;
  (void)buf;
  (void)size;
  FLASH_UNPORTED("bk_otp_apb_read");
  return BK_FAIL;
}

bk_err_t bk_otp_ahb_read(otp2_id_t item, uint8_t *buf, uint32_t size)
{
  (void)item;
  (void)buf;
  (void)size;
  FLASH_UNPORTED("bk_otp_ahb_read");
  return BK_FAIL;
}

bk_err_t bk_otp_ahb_update(otp2_id_t item, uint8_t *buf, uint32_t size)
{
  /* Refused: OTP is write-once. */

  (void)item;
  (void)buf;
  (void)size;
  return BK_FAIL;
}
#endif /* moved OTP/eFuse fallback */

/****************************************************************************
 * Watchdog
 ****************************************************************************/

bk_err_t bk_wdt_stop(void)
{
  /* The vendored PHY stops the watchdog around long calibration steps. NuttX
   * owns the BK7258 watchdog, and this port does not enable it during Wi-Fi
   * bring-up, so there is nothing to stop -- reporting success here would claim
   * authority this shim does not have.
   */

  return BK_FAIL;
}

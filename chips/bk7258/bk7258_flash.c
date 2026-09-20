/****************************************************************************
 * chips/bk7258/bk7258_flash.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "include/bk7258_flash.h"
#include "include/bk7258_memorymap.h"

#define BK7258_FLASH_OP_READ       5
#define BK7258_FLASH_OP_READ_ID    20

#define BK7258_FLASH_OP_SW         (UINT32_C(1) << 29)
#define BK7258_FLASH_BUSY          (UINT32_C(1) << 31)
#define BK7258_FLASH_OP_ADDR_MASK  UINT32_C(0x00ffffff)
#define BK7258_FLASH_OP_TYPE_SHIFT 24
#define BK7258_FLASH_OP_TYPE_MASK  UINT32_C(0x1f)

#define BK7258_FLASH_READ_UNIT     32
#define BK7258_FLASH_READ_WORDS    8
#define BK7258_FLASH_WAIT_LOOPS    1000000

static bool g_bk7258_flash_initialized;

static int bk7258_flash_wait_idle(void)
{
  unsigned int count;

  for (count = 0; count < BK7258_FLASH_WAIT_LOOPS; count++)
    {
      if ((getreg32(BK7258_FLASH_CTRL_OP_CTRL) & BK7258_FLASH_BUSY) == 0)
        {
          return OK;
        }
    }

  return -ETIMEDOUT;
}

static int bk7258_flash_start_operation(unsigned int opcode,
                                        uint32_t address)
{
  uint32_t command;
  int ret;

  ret = bk7258_flash_wait_idle();
  if (ret < 0)
    {
      return ret;
    }

  command = (address & BK7258_FLASH_OP_ADDR_MASK) |
            ((opcode & BK7258_FLASH_OP_TYPE_MASK) <<
             BK7258_FLASH_OP_TYPE_SHIFT);
  putreg32(command, BK7258_FLASH_CTRL_OP_CMD);
  modifyreg32(BK7258_FLASH_CTRL_OP_CTRL, 0, BK7258_FLASH_OP_SW);
  return bk7258_flash_wait_idle();
}

int bk7258_flash_initialize(void)
{
  /* The secure Bootloader has already initialized the shared Flash
   * controller before handing control to CP. Do not soft-reset or reconfigure
   * it here: CP code is executing from the same controller/XIP path. */

  g_bk7258_flash_initialized = true;
  return OK;
}

int bk7258_flash_read_id(uint32_t *id)
{
  irqstate_t flags;
  int ret;

  if (id == NULL)
    {
      return -EINVAL;
    }

  if (!g_bk7258_flash_initialized)
    {
      return -EIO;
    }

  flags = enter_critical_section();
  ret = bk7258_flash_start_operation(BK7258_FLASH_OP_READ_ID, 0);
  if (ret == OK)
    {
      *id = getreg32(BK7258_FLASH_CTRL_RD_FLASH_ID);
    }
  leave_critical_section(flags);
  return ret;
}

int bk7258_flash_read(uint32_t offset, void *buffer, size_t length)
{
  uint8_t *out = buffer;
  irqstate_t flags;
  uint32_t unit;

  if (length == 0)
    {
      return OK;
    }

  if (!g_bk7258_flash_initialized || buffer == NULL)
    {
      return buffer == NULL ? -EINVAL : -EIO;
    }

  if (offset >= BK7258_FLASH_SIZE_BYTES ||
      length > BK7258_FLASH_SIZE_BYTES - offset)
    {
      return -ERANGE;
    }

  flags = enter_critical_section();
  while (length > 0)
    {
      uint32_t aligned = offset & ~(BK7258_FLASH_READ_UNIT - 1);
      uint32_t skip = offset - aligned;
      uint32_t count = BK7258_FLASH_READ_UNIT - skip;
      uint32_t words[BK7258_FLASH_READ_WORDS];

      if (count > length)
        {
          count = length;
        }

      if (bk7258_flash_start_operation(BK7258_FLASH_OP_READ, aligned) < 0)
        {
          leave_critical_section(flags);
          return -ETIMEDOUT;
        }

      for (unit = 0; unit < BK7258_FLASH_READ_WORDS; unit++)
        {
          words[unit] = getreg32(BK7258_FLASH_CTRL_DATA_FLASH_SW);
        }

      memcpy(out, (uint8_t *)words + skip, count);
      out += count;
      offset += count;
      length -= count;
    }

  leave_critical_section(flags);
  return OK;
}

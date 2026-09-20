/****************************************************************************
 * chips/bk7258/bk7258_psram.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 DevKit PSRAM initialization. The command flow is derived from the
 * BK7258 CP implementation in Armino. It deliberately has no FreeRTOS,
 * persisted-ID, DMA, cache, or multi-image allocation dependencies.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_psram.h"

#define BK7258_PSRAM_APS6408L_ID       UINT16_C(0x8d09)
#define BK7258_PSRAM_APS128XXO_OB9_ID  UINT16_C(0x8d08)
#define BK7258_PSRAM_APS6408L_SIZE     (8u * 1024u * 1024u)
#define BK7258_PSRAM_APS128XXO_SIZE    (16u * 1024u * 1024u)

#define BK7258_PSRAM_MODE_APS6408L     UINT32_C(0xd8054041)
#define BK7258_PSRAM_MODE_APS128XXO    UINT32_C(0xd8054049)
#define BK7258_PSRAM_REG5_DRIVE        UINT32_C(0x380)
#define BK7258_PSRAM_COMMAND_LIMIT     50000u

static void bk7258_psram_delay(unsigned int loops)
{
  volatile unsigned int i;

  for (i = 0; i < loops; i++)
    {
      __asm__ volatile ("nop" : : : "memory");
    }
}

static int bk7258_psram_wait(uint32_t busy)
{
  unsigned int i;

  for (i = 0; i < BK7258_PSRAM_COMMAND_LIMIT; i++)
    {
      if ((getreg32(BK7258_PSRAM_REG8) & busy) == 0)
        {
          return OK;
        }
    }

  return -ETIMEDOUT;
}

static int bk7258_psram_command_read(uint32_t address, uint32_t *value)
{
  int ret;

  putreg32(address, BK7258_PSRAM_REG9);
  putreg32(BK7258_PSRAM_REG8_READ, BK7258_PSRAM_REG8);
  ret = bk7258_psram_wait(BK7258_PSRAM_REG8_READ);
  if (ret < 0)
    {
      return ret;
    }

  *value = getreg32(BK7258_PSRAM_REGB);
  return OK;
}

static int bk7258_psram_command_write(uint32_t address, uint32_t value)
{
  putreg32(address, BK7258_PSRAM_REG9);
  putreg32(value, BK7258_PSRAM_REGA);
  putreg32(BK7258_PSRAM_REG8_WRITE, BK7258_PSRAM_REG8);
  return bk7258_psram_wait(BK7258_PSRAM_REG8_WRITE);
}

static void bk7258_psram_power_clock_enable(void)
{
  /* Armino configures the DevKit PSRAM rail for its 1.8 V class setting
   * (PSLDO_SWB=1, VPSRAMSEL=0), enables the LDO, starts at 80 MHz, then
   * enables the peripheral clock. */

  modifyreg32(BK7258_SYS_ANA_REG13,
              BK7258_SYS_ANA_REG13_PSLDO_SWB |
              BK7258_SYS_ANA_REG13_VPSRAMSEL_MASK,
              BK7258_SYS_ANA_REG13_PSLDO_SWB);
  BK7258_BOOT_MARK('a');
  bk7258_psram_delay(10000);
  BK7258_BOOT_MARK('b');
  modifyreg32(BK7258_SYS_ANA_REG13, 0, BK7258_SYS_ANA_REG13_ENPSRAM);
  BK7258_BOOT_MARK('c');
  bk7258_psram_delay(20000);
  BK7258_BOOT_MARK('d');

  /* Power the AHBP domain, which owns the PSRAM controller.
   *
   * This step was missing.  The authority's psram_hal_power_clk_enable()
   * (cp/middleware/soc/common/hal/psram_hal.c) is:
   *
   *   sys_drv_psram_ldo_enable(1);
   *   bk_delay_us(1000);
   *   bk_pm_module_vote_power_ctrl(PM_POWER_SUB_MODULE_NAME_AHBP_PSRAM, ON);
   *   sys_drv_psram_psram_disckg(1);
   *   psram_hal_set_clk(PSRAM_80M);
   *   sys_drv_dev_clk_pwr_up(CLK_PWR_ID_PSRAM, CLK_PWR_CTRL_PWR_UP);
   *
   * and this port had the LDO, the clock source and the peripheral clock but
   * neither the power-domain vote nor the disckg.  It worked only because the
   * domain happens to come out of reset powered: nothing here re-established
   * it, so once the PM init switched AHBP off the PSRAM controller went with
   * it -- and with PSRAM being 16 MiB of the heap, that is what corrupted the
   * heap (see the note in bk7258_start.c).  Restoring these two lines makes
   * PSRAM's claim on the domain explicit, the way upstream has it.
   *
   * sys_hal_module_power_ctrl() rather than bk_pm_module_vote_power_ctrl():
   * the vote function in this port is hal_port/platform_shim.c:188, which handles
   * only WIFIP_MAC, the BAKP submodules and the PHY submodules and answers
   * BK_ERR_NOT_SUPPORT for everything else -- an AHBP vote there would be a
   * no-op today.  It also lives under if(CONFIG_BK7258_WIFI), and this file is
   * core boot code that must not depend on the Wi-Fi build.  The HAL entry is
   * authority code in pm/ and compiles unconditionally.
   *
   * The reference counting the vote layer would add is not needed here: PSRAM
   * is a permanent heap region from arm_addregion() onwards, so its claim never
   * drops.  When platform_shim's vote layer is replaced by the authority's
   * pm.c, this becomes the AHBP_PSRAM submodule vote and the call site is the
   * only line that changes.
   *
   * Declared locally for the same reason as in bk7258_start.c: including
   * sys_hal.h would pull the PM module's SoC headers into this file. */

  {
    extern void sys_hal_module_power_ctrl(int module, int power_state);
    extern void sys_hal_psram_psram_disckg(uint32_t value);

    /* POWER_MODULE_NAME_AHBP == 5, POWER_MODULE_STATE_ON == 0
     * (aon_pmu/.../sys_types.h:325 and :417).  Passed as int because the
     * enum types are in the header this file deliberately does not include;
     * both parameters are plain enums with values well inside int range. */

    sys_hal_module_power_ctrl(5 /* AHBP */, 0 /* STATE_ON */);

    /* Bus clock always open, as upstream does immediately after the vote. */

    sys_hal_psram_psram_disckg(1);
  }

  BK7258_BOOT_MARK('P');

  /* 80 MHz: select the 320 MHz source and divide by four. */

  modifyreg32(BK7258_SYS_CLKDIV2, BK7258_SYS_CLKDIV2_PSRAM_SEL,
              BK7258_SYS_CLKDIV2_PSRAM_DIV);
  BK7258_BOOT_MARK('e');
  modifyreg32(BK7258_SYS_DEV_CLK_EN, 0, BK7258_SYS_PSRAM_CLK_EN);
  BK7258_BOOT_MARK('f');
  bk7258_psram_delay(60000);
  BK7258_BOOT_MARK('g');
}

static void bk7258_psram_clock_120mhz(void)
{
  /* Armino's recommended default: 480 MHz source divided by four. */

  modifyreg32(BK7258_SYS_CLKDIV2, 0,
              BK7258_SYS_CLKDIV2_PSRAM_SEL |
              BK7258_SYS_CLKDIV2_PSRAM_DIV);
}

static int bk7258_psram_configure(uint16_t expected_id, uint32_t mode)
{
  uint32_t value;
  int ret;

  modifyreg32(BK7258_PSRAM_REG2, 0,
              BK7258_PSRAM_REG2_SF_RESET | BK7258_PSRAM_REG2_BYPASS);
  putreg32(mode, BK7258_PSRAM_REG4);
  putreg32(BK7258_PSRAM_REG5_DRIVE, BK7258_PSRAM_REG5);
  putreg32(BK7258_PSRAM_REG8_RESET, BK7258_PSRAM_REG8);
  bk7258_psram_delay(10000);

  ret = bk7258_psram_command_read(0, &value);
  if (ret < 0 || (uint16_t)value != expected_id)
    {
      return ret < 0 ? ret : -ENODEV;
    }

  value = getreg32(BK7258_PSRAM_REGB);
  if (expected_id == BK7258_PSRAM_APS6408L_ID)
    {
      value = (value & ~UINT32_C(0x1f)) | (UINT32_C(4) << 2) | UINT32_C(3);
    }
  else
    {
      value = (value & ~UINT32_C(0x1f)) | (UINT32_C(6) << 2) | UINT32_C(2);
    }

  ret = bk7258_psram_command_write(0, value);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_psram_command_read(4, &value);
  if (ret < 0)
    {
      return ret;
    }

  value = (getreg32(BK7258_PSRAM_REGB) & ~(UINT32_C(7) << 5)) |
          (UINT32_C(6) << 5);
  ret = bk7258_psram_command_write(4, value);
  if (ret < 0 || expected_id != BK7258_PSRAM_APS128XXO_OB9_ID)
    {
      return ret;
    }

  ret = bk7258_psram_command_read(8, &value);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_psram_command_write(8, getreg32(BK7258_PSRAM_REGB) |
                                     UINT32_C(0x40));
}

static int bk7258_psram_probe(size_t size)
{
  volatile uint32_t *memory = (volatile uint32_t *)BK7258_PSRAM_BASE;
  size_t words = CONFIG_BK7258_PSRAM_PROBE_SIZE / sizeof(*memory);
  size_t i;
  uint32_t saved0;
  uint32_t savedlast;
  const uint32_t patterns[] =
  {
    UINT32_C(0x00000000), UINT32_C(0xffffffff), UINT32_C(0x5aa55aa5)
  };

  if (words == 0 || CONFIG_BK7258_PSRAM_PROBE_SIZE > size)
    {
      return -EINVAL;
    }

  saved0 = memory[0];
  savedlast = memory[words - 1];

  for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
      memory[0] = patterns[i];
      memory[words - 1] = ~patterns[i];
      __asm__ volatile ("dsb" : : : "memory");

      if (memory[0] != patterns[i] || memory[words - 1] != ~patterns[i])
        {
          memory[0] = saved0;
          memory[words - 1] = savedlast;
          return -EIO;
        }
    }

  memory[0] = saved0;
  memory[words - 1] = savedlast;
  __asm__ volatile ("dsb" : : : "memory");
  return OK;
}

int bk7258_psram_initialize(size_t *size, uint16_t *device_id)
{
  int ret;
  size_t detected_size;
  uint16_t detected_id;

  if (size == NULL || device_id == NULL)
    {
      return -EINVAL;
    }

  BK7258_BOOT_MARK('1');
  bk7258_psram_power_clock_enable();
  BK7258_BOOT_MARK('2');

  ret = bk7258_psram_configure(BK7258_PSRAM_APS6408L_ID,
                               BK7258_PSRAM_MODE_APS6408L);
  BK7258_BOOT_MARK('3');
  if (ret == OK)
    {
      detected_id = BK7258_PSRAM_APS6408L_ID;
      detected_size = BK7258_PSRAM_APS6408L_SIZE;
    }
  else
    {
      BK7258_BOOT_MARK('4');
      ret = bk7258_psram_configure(BK7258_PSRAM_APS128XXO_OB9_ID,
                                   BK7258_PSRAM_MODE_APS128XXO);
      BK7258_BOOT_MARK('5');
      if (ret < 0)
        {
          return ret;
        }

      detected_id = BK7258_PSRAM_APS128XXO_OB9_ID;
      detected_size = BK7258_PSRAM_APS128XXO_SIZE;
    }

  /* Armino waits 1 ms after device configuration before moving from the
   * conservative 80 MHz setup clock to the 120 MHz operating clock. */

  up_mdelay(1);
  BK7258_BOOT_MARK('6');
  bk7258_psram_clock_120mhz();
  BK7258_BOOT_MARK('7');
  ret = bk7258_psram_probe(detected_size);
  BK7258_BOOT_MARK('8');
  if (ret < 0)
    {
      return ret;
    }

  *size = detected_size;
  *device_id = detected_id;
  return OK;
}

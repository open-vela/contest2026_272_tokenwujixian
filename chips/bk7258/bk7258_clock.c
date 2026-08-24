/****************************************************************************
 * chips/bk7258/bk7258_clock.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/spinlock.h>
#include <syslog.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_clock.h"
#include "include/bk7258_memorymap.h"

static int bk7258_clock_gate(uint32_t mask, bool enable)
{
  irqstate_t flags;
  int ret;

  flags = enter_critical_section();
  modifyreg32(BK7258_SYS_DEV_CLK_EN, mask, enable ? mask : 0);
  ret = ((getreg32(BK7258_SYS_DEV_CLK_EN) & mask) != 0) == enable ?
        OK : -EIO;
  leave_critical_section(flags);
  syslog(LOG_INFO, "[BK7258] clock gate mask=0x%08lx enable=%d ret=%d reg=0x%08lx\n",
         (unsigned long)mask, enable, ret,
         (unsigned long)getreg32(BK7258_SYS_DEV_CLK_EN));
  return ret;
}

int bk7258_mac_clock(bool enable)
{
  return bk7258_clock_gate(BK7258_SYS_MAC_CKEN, enable);
}

int bk7258_phy_clock(bool enable)
{
  return bk7258_clock_gate(BK7258_SYS_PHY_CKEN, enable);
}

void bk7258_clock_uart0(void)
{
  /* The secure Bootloader owns CPU/Flash clock setup for L0.  This layer only
   * enables the confirmed UART0 device clock and selects its independent
   * 26 MHz XTAL source. */

  uint32_t regval;

  /* This runs before NuttX's scheduler/locking initialization. */

  regval = getreg32(BK7258_SYS_DEV_CLK_EN);
  putreg32(regval | BK7258_SYS_UART0_CLK_EN, BK7258_SYS_DEV_CLK_EN);

  regval = getreg32(BK7258_SYS_CLKDIV1);
  putreg32(regval & ~BK7258_SYS_UART0_XTAL, BK7258_SYS_CLKDIV1);
}

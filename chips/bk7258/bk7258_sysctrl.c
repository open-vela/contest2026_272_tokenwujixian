/****************************************************************************
 * chips/bk7258/bk7258_sysctrl.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/spinlock.h>
#include <syslog.h>

#include "arm_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_sysctrl.h"

static int bk7258_power_gate(uint32_t mask, bool enable)
{
  irqstate_t flags;
  int ret;

  /* The Armino register is low-active: 1 means power down. */

  flags = enter_critical_section();
  modifyreg32(BK7258_SYS_POWER_WAKEUP, mask, enable ? 0 : mask);
  ret = ((getreg32(BK7258_SYS_POWER_WAKEUP) & mask) == 0) == enable ?
        OK : -EIO;
  leave_critical_section(flags);
  syslog(LOG_INFO, "[BK7258] power gate mask=0x%08lx enable=%d ret=%d reg=0x%08lx\n",
         (unsigned long)mask, enable, ret,
         (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP));
  return ret;
}

int bk7258_mac_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_WIFI_MAC_POWERDOWN, enable);
}

int bk7258_phy_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_WIFI_PHY_POWERDOWN, enable);
}

int bk7258_mac_reset(void)
{
  /* Armino sys_hal_mac_subsys_reset() is explicitly TODO on BK7258. */

  syslog(LOG_INFO, "[BK7258] MAC reset unsupported\n");
  return -ENOTSUP;
}

int bk7258_phy_reset(void)
{
  /* Armino sys_hal_modem_core/subsys_reset() are explicitly TODO on BK7258. */

  syslog(LOG_INFO, "[BK7258] PHY reset unsupported\n");
  return -ENOTSUP;
}

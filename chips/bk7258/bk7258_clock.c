/****************************************************************************
 * chips/bk7258/bk7258_clock.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <syslog.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_clock.h"
#include "include/bk7258_memorymap.h"

#ifndef CONFIG_BK7258_COMPONENT_AP
#  include <modules/pm.h>
#  include <sys_hal.h>
#endif

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

/* Armino's frequency ladder tops out at PM_CPU_FRQ_480M, which its
 * sys_hal.c implements as sys_hal_core_bus_clock_ctrl(0x3, 0x0, 0x0, 0x0, 0x1):
 * CPU0 240 MHz, CPU1/CPU2 480 MHz, BUS 240 MHz.  The vote goes through PM so
 * the VDDDIG level in sys_types.h's CPU_FREQ_VDDDIG_MAP is raised with it --
 * writing the dividers alone would overclock at the low voltage.
 *
 * The CP owns the shared clock: this must run before physical CPU1 is released,
 * so the AP never boots at one frequency and changes under its own feet.
 */



#ifndef CONFIG_BK7258_COMPONENT_AP

int bk7258_clock_cpu_boost(void)
{
  uint32_t hz_before = bk7258_clock_measure_hz();
  bk_err_t ret;
  uint32_t hz_after;

  /* Top step of the authority's ladder.
   *
   * pm.h:353-354 declares PM_CPU_FRQ_320M (CPU 320M / BUS 160M) and
   * PM_CPU_FRQ_480M (CPU 480M / BUS 240M), but
   * sys_hal_switch_cpu_bus_freq() only implements cases up to 320M: the 480M
   * entry falls through "default: break;" and returns OK without writing a
   * register.  That is why voting 480M here reported ret=0 twice while CPU0
   * stayed at 236 MHz and CPU1 at 119 MHz -- a successful-looking call that
   * configured nothing.
   *
   * 320M is therefore the highest step this SDK can actually apply, and its row
   * (sys_hal.c:515) is sys_hal_core_bus_clock_ctrl(0x3, 0x1, 0x0, 0x0, 0x1),
   * i.e. 480 MHz source, ckdiv_core = 1, ckdiv_cpu1 = 1 (the fast CPU1
   * setting).  Voting the step rather than poking dividers keeps the VDDDIG
   * raise that sys_hal_switch_cpu_bus_freq() performs per level.
   */

  ret = bk_pm_module_vote_cpu_freq(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_480M);
  hz_after = bk7258_clock_measure_hz();

  {
    uint32_t div = sys_hal_all_modules_clk_div_get(CLK_DIV_REG0);

    syslog(LOG_INFO,
           "[BK7258] cpu freq vote 480M ret=%d pm=%d clk_div_mode1=%08lx"
           " core_div=%lu core_sel=%lu bus_div=%lu\n",
           (int)ret,
           (int)bk_pm_module_current_cpu_freq_get(PM_DEV_ID_DEFAULT),
           (unsigned long)div,
           (unsigned long)(div & 0xfu),
           (unsigned long)((div >> 4) & 0x3u),
           (unsigned long)((div >> 6) & 0x1u));

    syslog(LOG_INFO,
           "[BK7258] measured core clock: before=%lu Hz after=%lu Hz\n",
           (unsigned long)hz_before, (unsigned long)hz_after);

    /* Register truth for both cores, independent of any timing basis:
     * sys_hal_cpu_clk_div_get() reads the per-core speed bits, and the raw
     * op words show halt/power/speed together.  The AP prints its own measured
     * frequency at boot, so those two together say whether CPU1 moved.
     */

    syslog(LOG_INFO,
           "[BK7258] per-core clock: cpu0_op=%08lx cpu1_op=%08lx "
           "div0=%lu div1=%lu\n",
           (unsigned long)getreg32(UINT32_C(0x44010010)),
           (unsigned long)getreg32(UINT32_C(0x44010014)),
           (unsigned long)sys_hal_cpu_clk_div_get(0),
           (unsigned long)sys_hal_cpu_clk_div_get(1));
  }

  return ret == BK_OK ? OK : -EIO;
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

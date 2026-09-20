/****************************************************************************
 * chips/bk7258/aon_rtc/nuttx_port/nuttx_port.c
 *
 * NuttX integration points required by the directly imported BK7258 authority
 * AON RTC module.  The authority driver/HAL/LL sources under ../authority are
 * unmodified behavior; this file only supplies the sys_ctrl-layer entry point
 * that the authority driver reaches through sys_driver.h but that this port
 * did not own yet.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <common/bk_include.h>

#include "sys_ll.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* CONFIG_RTC_ANA_WAKEUP_SUPPORT (authority BK7258 CP defconfig) compiles
 * bk_rtc_ana_register_wakeup_source(), whose PM enter callback calls
 * sys_drv_rtc_ana_wakeup_enable().  The authority chain is
 * sys_ps_driver.c:171 -> sys_pm_hal.c:1127 sys_hal_rtc_ana_wakeup_enable(),
 * which is a pure analog-SPI register sequence.  Neither the sys_ctrl driver
 * nor the PM HAL is imported into this port, so provide exactly that register
 * sequence here against the already-imported authority SYS LL accessors
 * instead of weakening the configuration gate.
 *
 * Reproduced verbatim from sys_pm_hal.c:1127-1141:
 *   enable SPI latch, ana_reg10.spi_timerwken = 1, ana_reg10.timer_sel =
 *   period, ana_reg12.clk_sel = 0 (ROSC; CONFIG_EXTERN_32K is not set for
 *   BK7258 CP), disable SPI latch.
 *
 * period encoding: 0 = 32ms, 5 = 1s, 6 = 2s ... 15 = 1024s.
 *
 * This is only reached from a super-deep-sleep enter callback.  This port has
 * no mapped Armino sleep transition yet, so it is registration-only today.
 */

void sys_drv_rtc_ana_wakeup_enable(uint32_t period)
{
  /* sys_hal_enable_spi_latch() (sys_pm_hal.c:265). */

  sys_ll_set_ana_reg9_spi_latch1v(1);

  sys_ll_set_ana_reg10_spi_timerwken(1);
  sys_ll_set_ana_reg10_timer_sel(period);

  /* CONFIG_EXTERN_32K is not set for BK7258 CP: select ROSC as RTC clock. */

  sys_ll_set_ana_reg12_clk_sel(0);

  /* sys_hal_disable_spi_latch() (sys_pm_hal.c:270). */

  sys_ll_set_ana_reg9_spi_latch1v(0);
}

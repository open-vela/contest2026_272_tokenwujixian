/****************************************************************************
 * chips/bk7258/bk7258_timer.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/timers/arch_timer.h>

#include "arm_internal.h"
#include "nvic.h"
#include "systick.h"
#include "include/bk7258_memorymap.h"

void up_timer_initialize(void)
{
  uint32_t regval;
  uint32_t reload;

  /* CP and AP use separate 32 kHz SysTick selectors for physical CPU0 and
   * physical CPU1. Do not touch the other domain's selector. */

  /* The Bootloader handoff does not define the SysTick state. Stop any
   * inherited counter and clear its pending exception before selecting the
   * source and letting the generic lower-half enable it. */

  putreg32(0, NVIC_SYSTICK_CTRL);
  putreg32(NVIC_INTCTRL_PENDSTCLR, NVIC_INTCTRL);

  regval = getreg32(BK7258_SYS_POWER_WAKEUP);
#ifdef CONFIG_BK7258_COMPONENT_AP
  regval |= BK7258_SYS_CPU1_SYSTICK_32K;
#else
  regval |= BK7258_SYS_CPU0_SYSTICK_32K;
#endif
  putreg32(regval, BK7258_SYS_POWER_WAKEUP);

  /* Seed the first interval before enabling SysTick so startup does not
   * depend on reset or Bootloader-retained reload/current values. */

  reload = CONFIG_BK7258_SYSTICK_CLOCK_HZ / CLK_TCK - 1;
  putreg32(reload, NVIC_SYSTICK_RELOAD);
  putreg32(0, NVIC_SYSTICK_CURRENT);

  /* SysTick must not retain the reset/Bootloader priority of zero. The
   * scheduler uses BASEPRI=0x80 for critical sections, so keep the first
   * timer interrupt at the normal NuttX system-handler priority. */

  up_prioritize_irq(NVIC_IRQ_SYSTICK, NVIC_SYSH_PRIORITY_DEFAULT);

  up_timer_set_lowerhalf(
    systick_initialize(false, CONFIG_BK7258_SYSTICK_CLOCK_HZ, -1));
}

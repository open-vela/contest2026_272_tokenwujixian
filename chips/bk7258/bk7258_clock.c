/****************************************************************************
 * chips/bk7258/bk7258_clock.c
 ****************************************************************************/

#include <nuttx/config.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"

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

/****************************************************************************
 * chips/bk7258/bk7258_gpio.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"

static void bk7258_gpio_uart0(unsigned int pin)
{
  uint32_t regval;

  /* GPIO11 uses peripheral function slot 0 for UART0 TX.  The per-pin GPIO
   * control enables the second function, disables GPIO ownership and keeps
   * the vendor's pull-up setting. */

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~(UINT32_C(3) << 2);
  regval |=  (UINT32_C(2) << 2);
  regval &= ~(UINT32_C(3) << 4);
  regval |=  (UINT32_C(3) << 4);
  regval |=  (UINT32_C(1) << 6);
  putreg32(regval, BK7258_GPIO_CFG(pin));

  regval  = getreg32(BK7258_SYS_GPIO_FUNC0 + 4);
  regval &= ~(UINT32_C(0xf) << BK7258_GPIO_FUNC_SHIFT(pin));
  putreg32(regval, BK7258_SYS_GPIO_FUNC0 + 4);
}

void bk7258_gpio_uart0_tx(void)
{
  bk7258_gpio_uart0(BK7258_GPIO11);
}

void bk7258_gpio_uart0_rx(void)
{
  bk7258_gpio_uart0(10);
}

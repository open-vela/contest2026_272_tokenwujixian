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

  regval  = getreg32(BK7258_SYS_GPIO_FUNC(pin));
  regval &= ~BK7258_GPIO_FUNC_MASK(pin);
  putreg32(regval, BK7258_SYS_GPIO_FUNC(pin));
}

void bk7258_gpio_uart0_tx(void)
{
  bk7258_gpio_uart0(BK7258_GPIO11);
}

void bk7258_gpio_uart0_rx(void)
{
  bk7258_gpio_uart0(10);
}

/****************************************************************************
 * Name: bk7258_gpio_select_func
 *
 * Description:
 *   Route the pin to its plain GPIO function (peripheral mux field = 0).
 *
 ****************************************************************************/

static void bk7258_gpio_select_func(unsigned int pin)
{
  uint32_t regval;
  uint32_t reg  = BK7258_SYS_GPIO_FUNC(pin);
  uint32_t mask = BK7258_GPIO_FUNC_MASK(pin);

  regval  = getreg32(reg);
  regval &= ~mask;
  putreg32(regval, reg);
}

/****************************************************************************
 * Name: bk7258_gpio_periph
 *
 * Description:
 *   Route the pin to a peripheral (second) function selected by the 4-bit
 *   per-pin mux field (see the Armino SDK gpio_map.h table, e.g. option 0
 *   on GPIO2/4 is the SPI1 SCK/MOSI).  The per-pin GPIO control is set to
 *   I/O-disable plus second-function so the peripheral owns the pad.
 *
 ****************************************************************************/

void bk7258_gpio_periph(unsigned int pin, unsigned int func)
{
  uint32_t regval;

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~(UINT32_C(3) << 2);        /* clear IO mode */
  regval |=  (UINT32_C(2) << 2);        /* I/O disable: peripheral drives */
  regval &= ~(UINT32_C(3) << 4);        /* clear pull */
  regval |=  (UINT32_C(1) << 6);        /* second function enable */
  regval &= ~BK7258_GPIO_CFG_OUTPUT_EN;
  regval &= ~BK7258_GPIO_CFG_INPUT_EN;
  putreg32(regval, BK7258_GPIO_CFG(pin));

  regval  = getreg32(BK7258_SYS_GPIO_FUNC(pin));
  regval &= ~BK7258_GPIO_FUNC_MASK(pin);
  regval |=  (func & UINT32_C(0xf)) << (((uint32_t)(pin) & 7) << 2);
  putreg32(regval, BK7258_SYS_GPIO_FUNC(pin));
}

/****************************************************************************
 * Name: bk7258_gpio_config_output
 *
 * Description:
 *   Configure the GPIO pin as a push-pull output with no pull resistor.
 *
 ****************************************************************************/

void bk7258_gpio_config_output(unsigned int pin)
{
  uint32_t regval;

  bk7258_gpio_select_func(pin);

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~BK7258_GPIO_CFG_MODE_MASK;
  regval |= BK7258_GPIO_CFG_MODE_OUTPUT;
  regval &= ~BK7258_GPIO_CFG_PULL_MASK;
  regval &= ~BK7258_GPIO_CFG_SECOND_FUNC;
  regval &= ~BK7258_GPIO_CFG_OUTPUT_EN;  /* LOW active: 0 = output enabled */
  regval &= ~BK7258_GPIO_CFG_INPUT_EN;   /* disable input path */
  putreg32(regval, BK7258_GPIO_CFG(pin));
}

/****************************************************************************
 * Name: bk7258_gpio_config_input
 *
 * Description:
 *   Configure the GPIO pin as an input with optional pull-up.
 *
 ****************************************************************************/

void bk7258_gpio_config_input(unsigned int pin)
{
  uint32_t regval;

  bk7258_gpio_select_func(pin);

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~BK7258_GPIO_CFG_MODE_MASK;
  regval |= BK7258_GPIO_CFG_MODE_INPUT;
  regval &= ~BK7258_GPIO_CFG_PULL_MASK;
  regval |= BK7258_GPIO_CFG_PULL_UP;
  regval &= ~BK7258_GPIO_CFG_SECOND_FUNC;
  regval |= BK7258_GPIO_CFG_OUTPUT_EN;  /* disable output path */
  regval |= BK7258_GPIO_CFG_INPUT_EN;   /* HIGH active: 1 = input enabled */
  putreg32(regval, BK7258_GPIO_CFG(pin));
}

/****************************************************************************
 * Name: bk7258_gpio_write
 *
 * Description:
 *   Drive a GPIO output pin to the requested level.
 *
 ****************************************************************************/

void bk7258_gpio_write(unsigned int pin, bool high)
{
  uint32_t regval;

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  if (high)
    {
      regval |= BK7258_GPIO_CFG_DATA;
    }
  else
    {
      regval &= ~BK7258_GPIO_CFG_DATA;
    }

  putreg32(regval, BK7258_GPIO_CFG(pin));
}

/****************************************************************************
 * Name: bk7258_gpio_read
 *
 * Description:
 *   Sample the level of a GPIO pin configured as an input.
 *
 ****************************************************************************/

bool bk7258_gpio_read(unsigned int pin)
{
  return (getreg32(BK7258_GPIO_CFG(pin)) & BK7258_GPIO_CFG_DATA) != 0;
}

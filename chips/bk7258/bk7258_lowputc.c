/****************************************************************************
 * chips/bk7258/bk7258_lowputc.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"

void bk7258_lowsetup(void)
{
  uint32_t config;
  uint32_t divider = CONFIG_BK7258_UART0_CLOCK_HZ / CONFIG_BK7258_UART0_BAUD - 1;

  bk7258_clock_uart0();
  bk7258_gpio_uart0_tx();

  putreg32(UINT32_C(1), BK7258_UART_GLOBAL_CTRL);
  putreg32(UINT32_C(0), BK7258_UART_INT_ENABLE);
  putreg32(UINT32_C(0xff), BK7258_UART_INT_STATUS);

  config = BK7258_UART_CONFIG_TX_EN | BK7258_UART_CONFIG_8BITS |
           BK7258_UART_CONFIG_DIV(divider);
  putreg32(config, BK7258_UART_CONFIG);
}

void bk7258_lowputc(char ch)
{
  while ((getreg32(BK7258_UART_FIFO_STATUS) & BK7258_UART_FIFO_WR_READY) == 0)
    {
    }

  putreg32((uint8_t)ch, BK7258_UART_FIFO_PORT);
}

void arm_lowputc(char ch)
{
  bk7258_lowputc(ch);
}

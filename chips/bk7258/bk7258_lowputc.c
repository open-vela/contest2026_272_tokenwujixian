/****************************************************************************
 * chips/bk7258/bk7258_lowputc.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_ap_boot.h"
#include "include/bk7258_memorymap.h"

/* The FIFO path goes through uart_ll, the same layer bk7258_uart.c and the
 * Armino uart_hal_* face use, so UART0's TX has one implementation.
 */

#include "uart_hal.h"
#include "uart_ll.h"

static uart_hw_t *const g_uart0_hw = (uart_hw_t *)BK7258_UART0_BASE;

void bk7258_lowsetup(void)
{
  uint32_t config;
  uint32_t divider = CONFIG_BK7258_UART0_CLOCK_HZ / CONFIG_BK7258_UART0_BAUD - 1;

  bk7258_clock_uart0();
  bk7258_gpio_uart0_tx();

  putreg32(UINT32_C(1), BK7258_UART_GLOBAL_CTRL);
  uart_ll_set_int_enable_status(g_uart0_hw, UART_ID_0, 0);
  uart_ll_clear_interrupt_status(g_uart0_hw, UART_ID_0, 0xff);

  config = BK7258_UART_CONFIG_TX_EN | BK7258_UART_CONFIG_8BITS |
           BK7258_UART_CONFIG_DIV(divider);
  putreg32(config, BK7258_UART_CONFIG);

  /* SWAP survives a reset: drop a token left by a previous AP panic. */

  bk7258_ap_uart_token_release();
}

void bk7258_lowputc(char ch)
{
  /* An AP panic dump owns the FIFO; dropping beats interleaving. */

  if (bk7258_ap_uart_token_held())
    {
      return;
    }

  while (!uart_ll_is_fifo_write_ready(g_uart0_hw, UART_ID_0))
    {
    }

  uart_ll_write_byte(g_uart0_hw, UART_ID_0, (uint8_t)ch);
}

void arm_lowputc(char ch)
{
  bk7258_lowputc(ch);
}

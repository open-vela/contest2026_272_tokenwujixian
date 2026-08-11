/****************************************************************************
 * chips/bk7258/bk7258_internal.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_INTERNAL_H
#define __VENDOR_BEKEN_CHIP_BK7258_INTERNAL_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <arch/irq.h>

void bk7258_lowsetup(void);
void bk7258_lowputc(char ch);
void bk7258_gpio_uart0_tx(void);
void bk7258_gpio_uart0_rx(void);
void bk7258_clock_uart0(void);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_INTERNAL_H */

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

/* Extended GPIO primitives from bk7258_gpio.h */

#include "include/bk7258_gpio.h"

/* Peripheral pin-mux helper retained for existing board users. */

void bk7258_gpio_periph(unsigned int pin, unsigned int func);

/* GPIO ICU source 55 and the aggregate AON pending registers are exclusively
 * AP-owned.  CP initialization returns -ENOSYS and CP dispatch does nothing,
 * preventing either routing or clearing AP interrupt state.
 */

int  bk7258_gpio_irq_initialize(void);
void bk7258_gpio_dispatch(void *context);

/* Board display SPI bit-bang lower-half (bk7258_spi_bitbang.c). */

struct spi_dev_s;
FAR struct spi_dev_s *bk7258_spi_initialize(void);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_INTERNAL_H */

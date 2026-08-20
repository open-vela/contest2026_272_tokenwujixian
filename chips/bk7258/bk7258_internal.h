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

/* Generic GPIO primitives used by board peripheral drivers. */

void bk7258_gpio_config_output(unsigned int pin);
void bk7258_gpio_config_input(unsigned int pin);
void bk7258_gpio_write(unsigned int pin, bool high);
bool bk7258_gpio_read(unsigned int pin);
void bk7258_gpio_periph(unsigned int pin, unsigned int func);

/* Board display SPI bit-bang lower-half (bk7258_spi_bitbang.c). */

struct spi_dev_s;
FAR struct spi_dev_s *bk7258_spi_initialize(void);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_INTERNAL_H */

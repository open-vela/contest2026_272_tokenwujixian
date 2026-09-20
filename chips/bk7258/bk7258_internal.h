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

/* Retained configurable-fault capture.  These wrappers preserve the generic
 * NuttX handlers after recording a small pre-panic snapshot in .noinit. */

int bk7258_hardfault(int irq, void *context, void *arg);
int bk7258_memfault(int irq, void *context, void *arg);
int bk7258_busfault(int irq, void *context, void *arg);
int bk7258_usagefault(int irq, void *context, void *arg);
int bk7258_securefault(int irq, void *context, void *arg);
void bk7258_fault_report_early(void);

/* Temporary CP-only boot diagnostics. Physical CPU0 owns UART0; the AP image
 * deliberately has no physical console, so its shared IRQ/timer code must not
 * acquire a dependency on bk7258_lowputc(). */
#ifdef CONFIG_BK7258_COMPONENT_CP
#  define BK7258_BOOT_MARK(ch) bk7258_lowputc(ch)
#else
#  define BK7258_BOOT_MARK(ch) do { } while (0)
#endif

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

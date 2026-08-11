/****************************************************************************
 * chips/bk7258/chip.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_CHIP_H
#define __VENDOR_BEKEN_CHIP_BK7258_CHIP_H

#include <nuttx/config.h>
#include <arch/irq.h>

#ifdef CONFIG_DEV_CONSOLE
#  undef USE_SERIALDRIVER
#  define USE_SERIALDRIVER 1
#endif

#define ARMV8M_PERIPHERAL_INTERRUPTS (NR_IRQS - 16)

#endif /* __VENDOR_BEKEN_CHIP_BK7258_CHIP_H */

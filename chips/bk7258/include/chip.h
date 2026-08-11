/****************************************************************************
 * chips/bk7258/include/chip.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_INCLUDE_CHIP_H
#define __VENDOR_BEKEN_CHIP_BK7258_INCLUDE_CHIP_H

#include <nuttx/config.h>
#include <arch/irq.h>

#define NVIC_SYSH_PRIORITY_MIN     0xe0
#define NVIC_SYSH_PRIORITY_DEFAULT 0x80
#define NVIC_SYSH_PRIORITY_MAX     0x00
#define NVIC_SYSH_PRIORITY_STEP    0x20
#define NVIC_SYSH_PRIORITY_SUBSTEP 0x10

#define ARMV8M_PERIPHERAL_INTERRUPTS (NR_IRQS - 16)

#endif /* __VENDOR_BEKEN_CHIP_BK7258_INCLUDE_CHIP_H */

/****************************************************************************
 * chips/bk7258/include/bk7258_irq.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_IRQ_DEFS_H
#define __VENDOR_BEKEN_CHIP_BK7258_IRQ_DEFS_H

#include <arch/irq.h>

#define BK7258_IRQ_FIRST NVIC_IRQ_FIRST

/* source is the BK7258 ICU source number from the SoC interrupt map. */

int bk7258_icu_attach(unsigned int source, xcpt_t handler, void *arg);
int bk7258_icu_enable(unsigned int source);
int bk7258_icu_disable(unsigned int source);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_IRQ_DEFS_H */

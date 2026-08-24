/****************************************************************************
 * chips/bk7258/include/irq.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_IRQ_H
#define __VENDOR_BEKEN_CHIP_BK7258_IRQ_H

#include <nuttx/config.h>

/* BK7258 ICU sources 0..63 map directly after the ARM exceptions. */

#define BK7258_IRQ_TIMER0  (16 + 3)
#define BK7258_IRQ_UART0   (16 + 4)
#define BK7258_IRQ_TIMER1  (16 + 13)
#define BK7258_IRQ_MODEM          (16 + 29)
#define BK7258_IRQ_MODEM_RC       (16 + 30)
#define BK7258_IRQ_MAC_TXRX_TIMER (16 + 31)
#define BK7258_IRQ_MAC_TXRX_MISC  (16 + 32)
#define BK7258_IRQ_MAC_RX_TRIGGER (16 + 33)
#define BK7258_IRQ_MAC_TX_TRIGGER (16 + 34)
#define BK7258_IRQ_MAC_PROT_TRIGGER (16 + 35)
#define BK7258_IRQ_MAC_GENERAL    (16 + 36)
#define BK7258_IRQ_MAC_HSU        (16 + 37)
#define BK7258_IRQ_MAC_WAKEUP     (16 + 38)
#define BK7258_IRQ_MAILBOX (16 + 63)

#define NR_IRQS            (16 + 64)

#endif /* __VENDOR_BEKEN_CHIP_BK7258_IRQ_H */

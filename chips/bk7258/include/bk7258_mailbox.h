/****************************************************************************
 * chips/bk7258/include/bk7258_mailbox.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_MAILBOX_H
#define __VENDOR_BEKEN_CHIP_BK7258_MAILBOX_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_BK7258_MAILBOX

#define BK7258_MBOX_RPTUN_MAGIC UINT32_C(0x5250544e) /* "RPTN" */

typedef void (*bk7258_mbox_callback_t)(uint8_t src_cpu, uint32_t data0,
                                       uint32_t data1, void *arg);

int bk7258_mbox_init(bool global_owner);
int bk7258_mbox_attach(bk7258_mbox_callback_t callback, void *arg);
int bk7258_mbox_notify(int dst_cpu, uint32_t token);

#endif /* CONFIG_BK7258_MAILBOX */
#endif /* __VENDOR_BEKEN_CHIP_BK7258_MAILBOX_H */

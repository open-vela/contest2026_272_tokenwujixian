/****************************************************************************
 * BK7258 Armino mailbox IPC lifecycle boundary.
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_MB_IPC_H
#define __VENDOR_BEKEN_CHIP_BK7258_MB_IPC_H

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_MB_IPC_RPMSG
int bk7258_mb_ipc_initialize(void);
#endif

#endif /* __VENDOR_BEKEN_CHIP_BK7258_MB_IPC_H */

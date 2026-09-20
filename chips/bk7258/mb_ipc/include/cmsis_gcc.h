#ifndef __BK7258_MB_IPC_CMSIS_GCC_H
#define __BK7258_MB_IPC_CMSIS_GCC_H

/* Imported mailbox_channel uses only the CMSIS data-memory barrier. NuttX
 * retains architecture ownership; this exposes the matching barrier at the
 * authority import boundary. */

#define __DMB() __asm__ volatile ("dmb" : : : "memory")

#endif /* __BK7258_MB_IPC_CMSIS_GCC_H */

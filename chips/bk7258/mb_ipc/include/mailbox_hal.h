#ifndef __BK7258_MB_IPC_MAILBOX_HAL_H
#define __BK7258_MB_IPC_MAILBOX_HAL_H

/* mailbox_driver_base.h declares mailbox_info_t but the imported channel
 * state machines never instantiate or access its HAL member. The RPMsg
 * backend owns transport, so only a complete placeholder type is needed. */

typedef struct
{
  void *hw;
  unsigned int id;
} mailbox_hal_t;

#endif /* __BK7258_MB_IPC_MAILBOX_HAL_H */

/*
 * chips/bk7258/wifi/hal_port/include/spinlock.h
 *
 * Spinlock types and macros for the vendored Armino glue.
 *
 * Armino ships two competing definitions, and both reach the compiled glue:
 *
 *   bk_wifi/include/wifi_spinlock.h      dummy: empty struct, no-op macros.
 *                                        Included by skbuff.h:5 (quoted) and
 *                                        rw_msg_rx.c:21.
 *   middleware/driver/spinlock/spinlock.h real: {owner,count,core_id} plus
 *                                        SPIN_LOCK_INIT. Included by
 *                                        bk_phy_adapter.c:46 and
 *                                        bk_rf_adapter.c:14 as "spinlock.h".
 *
 * Both typedef `spinlock_t`, which collides with NuttX's own (NuttX's arrives
 * via wpa_supplicant's os.h -> <stdio.h> -> nuttx/fs/fs.h). We cannot shadow
 * wifi_spinlock.h, because a quoted include searches the including file's own
 * directory first and skbuff.h sits right next to it. So the build predefines
 * its guard __SPIN_LOCK_H_ to skip that body, force-includes this header
 * instead, and this file is the single definition for both call sites.
 *
 * `spinlock_t` here IS NuttX's spinlock_t -- deliberately not a third
 * definition.
 *
 * Do not redefine NuttX's spin_lock* macros here. The active Wi-Fi sources
 * use NuttX as the lock owner. A future TX/RX port must convert Armino's
 * two-argument irq-save call sites explicitly rather than retain a semantic-
 * empty compatibility macro.
 */

#ifndef __BK7258_WIFI_GLUE_SPINLOCK_H
#define __BK7258_WIFI_GLUE_SPINLOCK_H

#include <nuttx/spinlock.h>

/* Armino's dummy header also declares these two; nothing in the compiled glue
 * references them, but keep the names available for source compatibility.
 */

typedef struct
{
  spinlock_t raw_lock;
} raw_spinlock_t;

typedef spinlock_t arch_spinlock_t;

#define SPIN_LOCK_FREE               (0xf2eef2ee)
#define SPINLOCK_FREE                SPIN_LOCK_FREE
#define SPIN_LOCK_INIT               SP_UNLOCKED
#define SPINLOCK_INITIALIZER         SP_UNLOCKED
#define SPIN_LOCK_ACQUIRE_INIT       SP_UNLOCKED
#define SPINLOCK_ACQUIRE_INITIALIZER SP_UNLOCKED
#define SPINLOCK_WAIT_FOREVER        (-1)
#define SPINLOCK_NO_WAIT             0

#endif /* __BK7258_WIFI_GLUE_SPINLOCK_H */

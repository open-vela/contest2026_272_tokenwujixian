#ifndef __SPIN_LOCK_H_
#define __SPIN_LOCK_H_

#include <nuttx/spinlock.h>

/* Use NuttX's spinlock_t definition and real IRQ-save semantics. */
#define bk_spin_lock_init(_lock)     spin_lock_init(_lock)
#define spin_lock_bh(_lock)          spin_lock(_lock)
#define spin_unlock_bh(_lock)        spin_unlock(_lock)
#define spin_lock_irqsave(_lock, flags) \
  do { (flags) = enter_critical_section(); spin_lock(_lock); } while (0)
#define spin_unlock_irqrestore(_lock, flags) \
  do { spin_unlock(_lock); leave_critical_section(flags); } while (0)
#endif // __SPIN_LOCK_H_

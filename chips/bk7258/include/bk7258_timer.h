#ifndef __VENDOR_BEKEN_CHIP_BK7258_TIMER_H
#define __VENDOR_BEKEN_CHIP_BK7258_TIMER_H

#include <stdint.h>

#include <nuttx/config.h>

/* Start/stop the independent BK7258 ICU Timer0 channel. */

int bk7258_timer_initialize(void);

#ifdef CONFIG_SMP
/* Start the per-core system tick on the AP SMP secondary core (physical
 * CPU2): core-clock SysTick, since the 32 kHz selector exists only for
 * physical CPU0/CPU1.  Runs on CPU2 from bk7258_cpu2_boot(). */

void bk7258_cpu2_timer_initialize(void);
#endif

#endif /* __VENDOR_BEKEN_CHIP_BK7258_TIMER_H */

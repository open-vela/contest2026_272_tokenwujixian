/****************************************************************************
 * chips/bk7258/include/bk7258_smp.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_SMP_H
#define __VENDOR_BEKEN_CHIP_BK7258_SMP_H

#include <nuttx/config.h>

#include <stdint.h>

/* AP SMP logical <-> physical core mapping.
 *
 * The AP component owns physical CPU1 and CPU2 inside one NuttX SMP image:
 *
 *   AP NuttX logical CPU0 -> BK7258 physical CPU1
 *   AP NuttX logical CPU1 -> BK7258 physical CPU2
 *
 * CP / physical CPU0 is a separate image and never enters this domain.
 */

#ifdef CONFIG_BK7258_COMPONENT_AP
#  define BK7258_AP_LOGICAL2PHYS(l)      ((l) + 1)
#  define BK7258_AP_PHYS2LOGICAL(p)      ((p) - 1)
#else
#  define BK7258_AP_LOGICAL2PHYS(l)      (l)
#  define BK7258_AP_PHYS2LOGICAL(p)      (p)
#endif

/* Secondary (physical CPU2) boot stack.  It carries the AP idle trampoline
 * until the first context switch, after which the per-CPU idle task stack
 * allocated by up_cpu_idlestack() takes over.
 */

#define BK7258_CPU2_BOOT_STACK_SIZE UINT32_C(2048)
extern uint8_t g_bk7258_cpu2_boot_stack[];
#define BK7258_CPU2_BOOT_STACK_TOP \
  ((uint32_t)((uintptr_t)g_bk7258_cpu2_boot_stack + \
              BK7258_CPU2_BOOT_STACK_SIZE))

/* CPU1 (AP logical0) detects itself without a hardware core-id register:
 * the running MSP equals either the CPU2 interrupt-stack top (after
 * arm_initialize_stack()) or the CPU2 reset boot-stack top (reset path).
 * Both are link-time constants distinct from every CPU1 stack. */

extern const uint32_t g_bk7258_cpu_intstack_top[];

#endif /* __VENDOR_BEKEN_CHIP_BK7258_SMP_H */

/****************************************************************************
 * chips/bk7258/bk7258_cpuindex.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/percpu.h>

#include "arm_internal.h"
#include "include/bk7258_smp.h"

#ifdef CONFIG_ARCH_HAVE_MULTICPU

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_cpu_index
 *
 * Description:
 *   Return the AP logical core number for the running core.
 *
 *   The AP component maps physical CPU1/CPU2 to NuttX logical CPU0/CPU1.
 *   BK7258 has no readable hardware core-id register, so the running core is
 *   identified by its private stack pointer instead: after
 *   arm_initialize_stack() the main stack pointer is pinned to the per-CPU
 *   interrupt stack, and on the reset path it equals the CPU2 boot stack.
 *   Both addresses are link-time constants distinct from every CPU1 stack,
 *   so the mapping is stable from the first C call on each core.
 *
 ****************************************************************************/

int up_cpu_index(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  /* CP is always physical CPU0 / NuttX logical CPU0. */

  return 0;
#else
#  ifdef CONFIG_SMP
  uint32_t msp;

  __asm__ volatile ("mrs %0, msp" : "=r"(msp) : : "memory");

#    if CONFIG_ARCH_INTERRUPTSTACK > 7
  if (msp == g_bk7258_cpu_intstack_top[1])
    {
      return 1;
    }
#    endif

  if (msp == BK7258_CPU2_BOOT_STACK_TOP)
    {
      return 1;
    }
#  endif

  return 0;
#endif
}

#endif /* CONFIG_ARCH_HAVE_MULTICPU */

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
   *   BK7258 has no readable hardware core-id register, so CPU2 is identified
   *   by its private boot or interrupt-stack range. The reset handler's C
   *   prologue consumes stack before this function can run, so an exact
   *   stack-top test would classify CPU2 as CPU1.
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

  /* CPU1 (logical0) runs with its MSP pinned to its own per-CPU interrupt
   * stack by arm_initialize_stack(); the task stacks live on PSP.  CPU2
   * (logical1) owns one contiguous stack region: its reset boot stack
   * followed by its per-CPU interrupt stack.  Test CPU1's interrupt stack
   * first so its top (which borders CPU2's intstack) can never be mistaken
   * for a CPU2 value, then test the whole CPU2 region. */

#    if CONFIG_ARCH_INTERRUPTSTACK > 7
  if (msp > g_bk7258_cpu_intstack_top[0] - INTSTACK_SIZE &&
      msp <= g_bk7258_cpu_intstack_top[0])
    {
      return 0;
    }
#    endif

  if (msp > BK7258_CPU2_BOOT_STACK_TOP - BK7258_CPU2_BOOT_STACK_SIZE &&
      msp <= g_bk7258_cpu_intstack_top[1])
    {
      return 1;
    }
#  endif

  return 0;
#endif
}

#endif /* CONFIG_ARCH_HAVE_MULTICPU */

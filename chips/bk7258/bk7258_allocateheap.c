/****************************************************************************
 * chips/bk7258/bk7258_allocateheap.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "arm_internal.h"
#include "include/bk7258_memorymap.h"

/* Guard this component's RAM window against the cross-domain regions. The
 * linker script cannot do this: including the chip header there would drag in
 * stdint.h and UINT32_C() suffixes that the linker rejects, so the check lives
 * here, next to the code that computes the heap's upper bound.
 *
 * PWR_MNG is a fixed-address CP/AP ABI and SWAP carries the AP boot record. A
 * component whose .data/.bss or heap grew over them would silently corrupt the
 * other domain rather than fail to link. Both checks are compile-time only and
 * generate no code.
 */

static_assert(CONFIG_RAM_START >= BK7258_SRAM_CAPACITY_BASE &&
              CONFIG_RAM_END <= BK7258_SRAM_CAPACITY_END,
              "BK7258 component RAM window falls outside the SoC SRAM");
static_assert(CONFIG_RAM_END <= BK7258_PWR_MNG_BASE,
              "BK7258 component RAM window overlaps PWR_MNG/SWAP");
static_assert(CONFIG_RAM_START >= BK7258_AP_SPINLOCK_BASE +
                                  BK7258_AP_SPINLOCK_SIZE,
              "BK7258 component RAM window overlaps AP_SPINLOCK");

#ifdef CONFIG_BK7258_COMPONENT_AP
static_assert(CONFIG_RAM_START == BK7258_AP_RAM_BASE,
              "BK7258 AP RAM start differs from the all-OpenVela tuple");
static_assert(CONFIG_RAM_SIZE == BK7258_AP_RAM_SIZE,
              "BK7258 AP RAM size differs from the all-OpenVela tuple");
static_assert(CONFIG_RAM_END == CONFIG_BK7258_RPMSG_SHM_ADDR,
              "BK7258 AP RAM must end where RPMSG_SHM begins");
#else
static_assert(CONFIG_RAM_START == CONFIG_BK7258_RPMSG_SHM_ADDR +
                                  CONFIG_BK7258_RPMSG_SHM_SIZE,
              "BK7258 CP RAM must begin after RPMSG_SHM");
static_assert(CONFIG_RAM_END == BK7258_PWR_MNG_BASE,
              "BK7258 CP RAM must end where PWR_MNG begins");
#endif

const uintptr_t g_idle_topstack =
  (uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE;

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  /* Bound the heap with CONFIG_RAM_END, as the upstream arch allocators do.
   * It is derived from this component's own CONFIG_RAM_START/CONFIG_RAM_SIZE,
   * so CP and AP each get their configured window with nothing to keep in sync
   * by hand, and neither can grow into the cross-domain PWR_MNG/SWAP regions.
   */

  *heap_start = (void *)g_idle_topstack;
  *heap_size = CONFIG_RAM_END - g_idle_topstack;
}

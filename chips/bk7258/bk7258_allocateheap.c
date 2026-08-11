/****************************************************************************
 * chips/bk7258/bk7258_allocateheap.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include "arm_internal.h"
#include "include/bk7258_memorymap.h"

const uintptr_t g_idle_topstack = (uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE;

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  *heap_start = (void *)g_idle_topstack;
  *heap_size = BK7258_SRAM_END - g_idle_topstack;
}

/****************************************************************************
 * chips/bk7258/bk7258_allocateheap.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/kmalloc.h>
#include <syslog.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_psram.h"

/* The AP does not re-run the device bring-up; it sizes its half from the
 * same device table bk7258_psram.c probes with.  Both known devices are
 * 16MB on this board (APS128XXO); the split halves whatever is present. */
#define BK7258_PSRAM_EXPECTED_SIZE \
  (16u * 1024u * 1024u)

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

#ifdef CONFIG_BK7258_COMPONENT_CP
static void bk7258_heap_mark_hex(uintptr_t value)
{
  static const char hex[] = "0123456789ABCDEF";
  int shift;

  for (shift = (int)(sizeof(value) * 8) - 4; shift >= 0; shift -= 4)
    {
      bk7258_lowputc(hex[(value >> shift) & 0xf]);
    }
}
#endif

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  /* Bound the heap with CONFIG_RAM_END, as the upstream arch allocators do.
   * It is derived from this component's own CONFIG_RAM_START/CONFIG_RAM_SIZE,
   * so CP and AP each get their configured window with nothing to keep in sync
   * by hand, and neither can grow into the cross-domain PWR_MNG/SWAP regions.
   */

  *heap_start = (void *)g_idle_topstack;
  *heap_size = CONFIG_RAM_END - g_idle_topstack;

  /* Temporary BK-only allocator evidence. This runs after lowsetup and before
   * common driver allocations, so it proves the precise heap handed to the
   * first RPMsg UART allocations without requiring syslog or malloc itself. */
#ifdef CONFIG_BK7258_COMPONENT_CP
  BK7258_BOOT_MARK('H');
  bk7258_heap_mark_hex((uintptr_t)*heap_start);
  BK7258_BOOT_MARK(':');
  bk7258_heap_mark_hex((uintptr_t)*heap_size);
  BK7258_BOOT_MARK('\r');
  BK7258_BOOT_MARK('\n');
#endif
}

#if CONFIG_MM_REGIONS > 1
void arm_addregion(void)
{
#ifdef CONFIG_BK7258_PSRAM
  size_t psram_size;
  uint16_t device_id;
  uintptr_t region_base;
  size_t region_size;
  int ret;

  /* Temporary CP-only PSRAM bring-up bracket.  arm_addregion() runs after
   * the F timer marker and before serial initialization's G marker, so this
   * identifies whether the added external-heap path returns at all. */
  BK7258_BOOT_MARK('P');
#ifdef CONFIG_BK7258_COMPONENT_AP
  /* AP: the CP has already configured the device and the 120MHz clock.  A
   * plain read of the device answers only when that bring-up happened; do
   * not pattern-write anything (the probe would smash the CP's half). */
  {
    volatile uint32_t *probe = (volatile uint32_t *)BK7258_PSRAM_BASE;
    uint32_t dummy = *probe;

    UNUSED(dummy);
    psram_size = BK7258_PSRAM_EXPECTED_SIZE;
    device_id  = 0;
    ret        = OK;
  }
#else
  ret = bk7258_psram_initialize(&psram_size, &device_id);
#endif
  BK7258_BOOT_MARK('R');
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "[BK7258] PSRAM unavailable (%d); using internal SRAM heap only\n",
             ret);
      return;
    }

  /* The PSRAM is a single 8/16MB device shared by both cores; neither core
   * has an MPU partition (CONFIG_ARM_MPU unset), so the only thing keeping
   * the two kernels' allocators apart is this software split.  Each core
   * adds only its half: the CP owns the lower half, the AP the upper one.
   * The CP brings the device up first (it boots first).  The AP must not
   * re-run the bring-up: bk7258_psram_probe() pattern-writes the first
   * CONFIG_BK7258_PSRAM_PROBE_SIZE bytes at the device base -- CP-owned
   * heap by then -- and the configure/clock sequence is already settled.
   * The AP trusts the CP's bring-up, checks that the device answers, and
   * adds only its own half. */

#if defined(CONFIG_BK7258_COMPONENT_CP)
  region_base = BK7258_PSRAM_BASE;
  region_size = psram_size / 2;
#elif defined(CONFIG_BK7258_COMPONENT_AP)
  region_base = BK7258_PSRAM_BASE + (psram_size / 2);
  region_size = psram_size - (psram_size / 2);
#else
#error "PSRAM split requires a BK7258 component selection (CP or AP)"
#endif

  kumm_addregion((void *)region_base, region_size);
  syslog(LOG_INFO, "[BK7258] PSRAM id=0x%04x size=%lu split: core region "
         "%08lx-%08lx added to heap\n",
         device_id, (unsigned long)psram_size,
         (unsigned long)region_base,
         (unsigned long)(region_base + region_size - 1));
#endif
}
#endif

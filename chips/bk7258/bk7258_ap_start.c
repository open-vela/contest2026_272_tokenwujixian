/****************************************************************************
 * chips/bk7258/bk7258_ap_start.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/cache.h>
#include <nuttx/init.h>

#include "arm_internal.h"
#include "nvic.h"
#include "include/bk7258_ap_boot.h"
#include "include/bk7258_memorymap.h"

#ifdef CONFIG_BK7258_MAILBOX
#  include "include/bk7258_mailbox.h"
#endif

extern const void *const _vectors[];

const struct bk7258_ap_image_contract_s g_bk7258_ap_image_contract
  __attribute__((section(".ap_contract"), used)) =
{
  .magic = BK7258_AP_IMAGE_MAGIC,
  .abi_version = BK7258_AP_BOOT_ABI_VERSION,
  .build_id = BK7258_AP_BUILD_ID,
  .vector_base = BK7258_AP_XIP_VECTOR_BASE,
};

void __start(void)
{
  const uint32_t *source;
  uint32_t *destination;

  __asm__ volatile ("cpsid i" : : : "memory");

  /* CPU1 may inherit cache state from the Bootloader/vendor AP lifecycle.
   * A2-1 has no cache-coherent Mailbox transport yet, so use a deterministic
   * uncached shared-SRAM path for SWAP and RPMSG_SHM. */

  if ((getreg32(NVIC_CFGCON) & NVIC_CFGCON_DC) != 0)
    {
      up_disable_dcache();
    }

  /* CPU1 has a private I-cache and starts from reset rather than inheriting
   * CPU0's Bootloader execution state. Match the vendor CPU1 startup contract
   * before executing the TIMER_ARCH and RPMsg paths from XIP. */

  up_enable_icache();

  putreg32((uintptr_t)_vectors, NVIC_VECTAB);
  __asm__ volatile ("dsb\n\tisb" : : : "memory");

#ifdef CONFIG_BK7258_MAILBOX
  /* A warm reset keeps the SYS-level mailbox routes of the previous boot
   * alive while SRAM is cleared.  Disable both AP routes here so no stale
   * doorbell can pend or fire anywhere in the bring-up window;
   * bk7258_mbox_init() drains the stale FIFO entries and re-arms them. */

  modifyreg32(BK7258_SYS_CPU1_INT_EN_HI, BK7258_SYS_MAILBOX_INT_EN, 0);
  modifyreg32(BK7258_SYS_CPU2_INT_EN_HI, BK7258_SYS_MAILBOX_INT_EN, 0);
#endif

#ifdef CONFIG_SMP
  /* Pin CPU1's interrupt stack and select PSP for thread mode.  Without
   * this, CPU1's thread stack runs on the initial MSP and can grow down into
   * CPU2's stack region, corrupting the MSP-based core identification used by
   * up_cpu_index().  With the MSP fixed to CPU1's own interrupt stack, the
   * running (PSP) stacks never overlap CPU2's region. */

  arm_initialize_stack();
#endif

  for (destination = (uint32_t *)_sbss;
       destination < (uint32_t *)_ebss; )
    {
      *destination++ = 0;
    }

  for (source = (const uint32_t *)_eronly,
       destination = (uint32_t *)_sdata;
       destination < (uint32_t *)_edata; )
    {
      *destination++ = *source++;
    }

  bk7258_ap_record_mark_reset((uint32_t)(uintptr_t)_vectors,
                              (uint32_t)(uintptr_t)_vectors[1]);
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_RESET_DONE);

  /* No notifier registration here: anything that takes sched_lock() before
   * nx_start()'s tasklist_initialize() dereferences a NULL TCB inside
   * _assert() and double-faults into a silent lockup. */

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_BEFORE_NX);
  nx_start();

  for (; ; )
    {
    }
}

void arm_lowputc(char ch)
{
  (void)ch;
}

void up_putc(int ch)
{
  (void)ch;
}

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

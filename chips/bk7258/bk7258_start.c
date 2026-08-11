/****************************************************************************
 * chips/bk7258/bk7258_start.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/init.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_internal.h"

extern const void *const _vectors[];

void __start(void)
{
  const uint32_t *src;
  uint32_t *dest;

  /* Keep all exceptions masked until the vector base and C runtime state are
   * coherent. This first CP L0 image intentionally has no RAM-vector or NS
   * handoff path. */

  /* BASEPRI alone leaves high-priority configurable IRQs unmasked. The
   * opaque Bootloader may have left routes enabled, so use PRIMASK until
   * up_irqinitialize installs the NuttX vector/handler state. */

  __asm__ volatile ("cpsid i" : : : "memory");
  putreg32((uintptr_t)_vectors, NVIC_VECTAB);
  __asm__ volatile ("dsb\n\tisb" : : : "memory");

  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }

  for (src = (const uint32_t *)_eronly, dest = (uint32_t *)_sdata;
       dest < (uint32_t *)_edata; )
    {
      *dest++ = *src++;
    }

  bk7258_lowsetup();
  bk7258_lowputc('B');
  bk7258_lowputc('K');
  bk7258_lowputc('\r');
  bk7258_lowputc('\n');

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  nx_start();

  for (; ; )
    {
    }
}

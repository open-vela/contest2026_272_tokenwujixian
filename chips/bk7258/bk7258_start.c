/****************************************************************************
 * chips/bk7258/bk7258_start.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/cache.h>
#include <nuttx/init.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_internal.h"

extern const void *const _vectors[];

/* Early-fault handlers are normally attached in up_irqinitialize(), which runs
 * deep inside nx_start().  Until then g_irqvector[] is zero (BSS) and any
 * HardFault/MemManage/BusFault falls through to irq_unexpected_isr, which
 * prints only "irq: 3" with no CFSR/BFAR/fault-PC.  Attach them here, after
 * the .bss clear and .data copy (g_irqvector lives in .bss, so attaching
 * earlier would be wiped) but before bk7258_lowsetup/nx_start, so any early
 * boot HardFault prints the real fault dump instead of a silent hang.
 * up_irqinitialize() later re-attaches the same handlers (harmless).
 */
extern int arm_hardfault(int irq, void *context, void *arg);
extern int arm_memfault(int irq, void *context, void *arg);
extern int arm_busfault(int irq, void *context, void *arg);
extern int arm_usagefault(int irq, void *context, void *arg);
#ifdef CONFIG_ARMV8M_SECUREFAULT
extern int arm_securefault(int irq, void *context, void *arg);
#endif

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

  /* The locked Bootloader/vendor tuple enables D-cache. NuttX replaces that
   * runtime and the RPMsg transport deliberately uses uncached shared SRAM,
   * so clean and disable any inherited D-cache before touching .data, .bss,
   * SWAP or RPMSG_SHM. */

  if ((getreg32(NVIC_CFGCON) & NVIC_CFGCON_DC) != 0)
    {
      up_disable_dcache();
    }

  /* Make instruction fetch deterministic rather than inheriting an opaque
   * Bootloader cache state. The BK7258 TIMER_ARCH time read must complete its
   * resource/status snapshot between two SysTick updates. */

  up_enable_icache();

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

  /* Attach fault handlers now, AFTER .bss clear and .data copy (g_irqvector
   * lives in .bss, so attaching earlier would be wiped by the BSS clear).
   * This runs before bk7258_lowsetup and before nx_start, so any early boot
   * HardFault/MemManage/BusFault prints the real CFSR/BFAR/fault-PC dump
   * instead of falling through to the default irq_unexpected_isr "irq: 3".
   * up_irqinitialize() later re-attaches the same handlers (harmless).
   */
  irq_attach(NVIC_IRQ_HARDFAULT, arm_hardfault, NULL);
  irq_attach(NVIC_IRQ_MEMFAULT, arm_memfault, NULL);
  irq_attach(NVIC_IRQ_BUSFAULT, arm_busfault, NULL);
  irq_attach(NVIC_IRQ_USAGEFAULT, arm_usagefault, NULL);
#ifdef CONFIG_ARMV8M_SECUREFAULT
  irq_attach(NVIC_IRQ_SECUREFAULT, arm_securefault, NULL);
#endif

  bk7258_lowsetup();
  bk7258_lowputc('B');
  bk7258_lowputc('K');
  bk7258_lowputc('\r');
  bk7258_lowputc('\n');

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  /* Trace marker: boot reached nx_start. */
  bk7258_lowputc('S');
  bk7258_lowputc('>');

  nx_start();

  for (; ; )
    {
    }
}

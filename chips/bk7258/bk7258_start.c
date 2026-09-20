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
#include "include/bk7258_memorymap.h"

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
extern int arm_memfault(int irq, void *context, void *arg);
extern int arm_busfault(int irq, void *context, void *arg);
extern int arm_usagefault(int irq, void *context, void *arg);

static void bk7258_start_puthex(uint32_t value)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for (shift = 28; shift >= 0; shift -= 4)
    {
      bk7258_lowputc(hex[(value >> shift) & 0xf]);
    }
}

static void bk7258_report_reset_context_early(void)
{
  uint32_t r0 = getreg32(BK7258_AON_PMU_R0);

  /* This is a read-only snapshot.  PMU R0 is retained reset-classification
   * state consumed by ROM/BL2; never clear or rewrite it while diagnosing a
   * post-TPC restart. */

  bk7258_lowputc('!');
  bk7258_lowputc('R');
  bk7258_lowputc('S');
  bk7258_lowputc('T');
  bk7258_lowputc(' ');
  bk7258_lowputc('r');
  bk7258_lowputc('0');
  bk7258_lowputc('=');
  bk7258_start_puthex(r0);
  bk7258_lowputc(' ');
  bk7258_lowputc('c');
  bk7258_lowputc('p');
  bk7258_lowputc('=');
  bk7258_start_puthex((r0 >> 4) & UINT32_C(0xff));
  bk7258_lowputc(' ');
  bk7258_lowputc('a');
  bk7258_lowputc('p');
  bk7258_lowputc('=');
  bk7258_start_puthex((r0 >> 24) & UINT32_C(0x7f));
  bk7258_lowputc('\r');
  bk7258_lowputc('\n');
}

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
  irq_attach(NVIC_IRQ_HARDFAULT, bk7258_hardfault, NULL);
  irq_attach(NVIC_IRQ_MEMFAULT, bk7258_memfault, NULL);
  irq_attach(NVIC_IRQ_BUSFAULT, bk7258_busfault, NULL);
  irq_attach(NVIC_IRQ_USAGEFAULT, bk7258_usagefault, NULL);
  irq_attach(NVIC_IRQ_SECUREFAULT, bk7258_securefault, NULL);

  /* A hard-float image can use the VFP calling convention before nx_start()
   * enters the generic initialization path.  Configure CP10/CP11 and the
   * NuttX FP context policy while interrupts are still masked, after .data
   * and .bss are valid but before any board peripheral or PSRAM work. */

#ifdef CONFIG_ARCH_FPU
  arm_fpuconfig();
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
#endif

  bk7258_lowsetup();
  bk7258_report_reset_context_early();
  bk7258_fault_report_early();
  bk7258_lowputc('B');
  bk7258_lowputc('K');
  bk7258_lowputc('\r');
  bk7258_lowputc('\n');

  /* Early-boot diagnostic ladder.  These polling UART markers deliberately
   * bypass syslog, the serial driver, IRQs, and the scheduler.  They identify
   * whether a BK-only boot stops in arm_earlyserialinit() or after nx_start()
   * is entered.  Remove after the runtime-on cold-boot investigation. */

  bk7258_lowputc('1');

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  bk7258_lowputc('2');

  /* Power-manager hardware init, at the authority's position in the boot.
   *
   * Upstream calls this one line ahead of the RTOS entry point:
   *
   *   pm_hardware_init();                     <- startup_cpu0.c:402
   *   bk_pm_cp1_auto_power_down_state_set(PM_CP1_AUTO_CTRL_DISABLE);
   *   bk_pm_mem_auto_power_down_state_set(PM_MEM_AUTO_CTRL_DISABLE);
   *   entry_main();                           <- startup_cpu0.c:407
   *
   * and pm_hardware_init() (pm.c:213) reaches sys_hal_low_power_hardware_init()
   * through sys_drv_low_power_hardware_init() (sys_ps_driver.c:191).  nx_start()
   * is our entry_main(), so this is the equivalent slot.
   *
   * Why the position is load-bearing rather than cosmetic.  The function powers
   * the AHBP domain OFF, and PSRAM is one of that domain's sub-modules
   * (POWER_SUB_MODULE_NAME_AHBP_PSRAM, sys_types.h:407).  PSRAM is also 16 MiB
   * of our heap -- 99.5% of it -- added by arm_addregion() ->
   * bk7258_psram_initialize() -> kumm_addregion() (bk7258_allocateheap.c:116).
   * arm_addregion() runs from up_initialize() (arm_initialize.c:65), i.e. INSIDE
   * nx_start(), so calling this before nx_start() means the domain is switched
   * off while PSRAM is still nobody's memory, and bk7258_psram_initialize()
   * powers it back on immediately afterwards -- the same order upstream has
   * (startup switches AHBP off, bk_psram_init() votes AHBP_PSRAM back on).
   *
   * It used to be called from bk7258_wifi_initialize() instead, which is after
   * PSRAM has been in the heap for the whole boot.  That is what corrupted the
   * heap: switching the domain off took the contents of ~16 MiB of live heap
   * with it, so mm_foreach() then asserted on the first inconsistent node
   * (nuttx/mm/mm_heap/mm_foreach.c:120) while the ALLOCATOR's accounting still
   * looked perfect -- free= was byte-identical across every probe.  Established
   * by two board experiments: with the whole call skipped the heap survived 40
   * walks (/tmp/0910-vela-9.log), and with the call restored but only the AHBP
   * line disabled it survived 76 (/tmp/0910-vela-10.log).
   *
   * Safe this early: every callee is a sys_ll_ or aon_pmu_ll_ register write to
   * the always-on SYS (0x44010000) and AON PMU (0x44000000) domains, and the
   * only timing dependency is bk_delay_us() -> up_udelay(), a busy loop over
   * the compile-time CONFIG_BOARD_LOOPSPERMSEC (arm_udelay.c:57) that needs
   * neither a timer nor the scheduler.  AHBP being off is separately known to
   * be survivable here -- the board kept running and kept printing with it off
   * in /tmp/0910-vela-8.log.
   *
   * Declared locally rather than by including sys_hal.h: that header pulls in
   * the PM module's SoC headers, and this file deliberately includes nothing
   * but NuttX and the chip's own memory map. */

  {
    extern void sys_hal_low_power_hardware_init(void);

    sys_hal_low_power_hardware_init();
  }

  /* Trace marker: boot reached nx_start. */
  bk7258_lowputc('S');
  bk7258_lowputc('>');

  nx_start();

  for (; ; )
    {
    }
}

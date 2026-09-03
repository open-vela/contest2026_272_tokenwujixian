/****************************************************************************
 * chips/bk7258/bk7258_smp.c
 *
 * AP SMP port: physical CPU2 launch, per-CPU interrupt stacks, IPI and
 * secondary-core boot.  CP stays AMP: this file only affects the AP image.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_SMP

#include <stdint.h>
#include <strings.h>

#include <nuttx/arch.h>
#include <nuttx/compiler.h>
#include <nuttx/irq.h>
#include <nuttx/sched_note.h>

#include "init/init.h"

#include "arm_internal.h"
#include "nvic.h"
#include "ram_vectors.h"
#include "chip.h"
#include "sched/sched.h"
#include "include/bk7258_ap_boot.h"
#include "include/bk7258_mailbox.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_smp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_CPU2_CTRL_LIFECYCLE_MASK \
  (BK7258_SYS_CPU2_RESET_RELEASE | BK7258_SYS_CPU2_POWER_DOWN | \
   BK7258_SYS_CPU2_HALT | BK7258_SYS_CPU2_RXEVT_SEL | \
   BK7258_SYS_CPU2_OFFSET_MASK)

#define BK7258_CPU2_POWER_STABILIZE_LOOPS 1000

/* SWAP handshake poll bound.  The loop body is one SRAM read from XIP code,
 * so this is on the order of a second -- long enough for CPU2 to leave
 * reset and finish its early boot, short enough to fail visibly instead of
 * hanging the AP primary core. */

#define BK7258_CPU2_HANDSHAKE_LOOPS       20000000

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Per-CPU interrupt stacks.  In an SMP configuration the common ARMv8-M
 * arm_exception.S does not emit g_intstackalloc, so the chip provides one
 * aligned stack per logical CPU.  The const tops are link-time constants and
 * therefore valid from the very first C instruction on each core, which is
 * exactly what up_cpu_index() relies on. */

#if CONFIG_ARCH_INTERRUPTSTACK > 7
static uint64_t g_bk7258_intstack_alloc[(CONFIG_SMP_NCPUS * INTSTACK_SIZE) >> 3];

const uint32_t g_bk7258_cpu_intstack_top[CONFIG_SMP_NCPUS] =
{
  (uint32_t)g_bk7258_intstack_alloc + INTSTACK_SIZE,
#if CONFIG_SMP_NCPUS > 1
  (uint32_t)g_bk7258_intstack_alloc + (2 * INTSTACK_SIZE),
#endif
};
#endif

/* Secondary boot stack.  .noinit keeps it out of the zeroed .bss so the CPU2
 * reset path (before .bss is ready on CPU2) can push onto it. */

uint8_t g_bk7258_cpu2_boot_stack[BK7258_CPU2_BOOT_STACK_SIZE]
  __attribute__((section(".noinit"), aligned(8)));

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

extern void exception_common(void);
extern void exception_direct(void);

static void bk7258_cpu2_boot(void) __attribute__((noreturn));

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Secondary (physical CPU2) vector table.  The hardware reads only the first
 * two entries at reset; every other slot is filled with the shared direct
 * handler so a spurious exception on CPU2 before VTOR is reprogrammed still
 * lands in NuttX code.  It must stay 512-byte aligned in AP XIP. */

const void *const _vectors_core1[]
  locate_data(".vectors_core1") aligned_data(VECTAB_ALIGN) =
{
  /* Initial stack: dedicated CPU2 boot stack, distinct from every CPU1 stack
   * so up_cpu_index() can identify this core from the reset MSP. */

  (const void *)BK7258_CPU2_BOOT_STACK_TOP,

  /* Reset handler: CPU2 secondary boot entry. */

  (const void *)bk7258_cpu2_boot,

  /* Vectors 2 - n point directly at the generic handler. */

  [2 ... NVIC_IRQ_PENDSV] = &exception_common,
  [(NVIC_IRQ_PENDSV + 1) ... (15 + ARMV8M_PERIPHERAL_INTERRUPTS)]
                          = &exception_direct
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_cpu2_boot
 *
 * Description:
 *   Secondary (physical CPU2) reset entry.  .bss/.data are already
 *   initialized by CPU1; this core only needs its own stacks, NVIC, VTOR
 *   and IPI doorbell before handing over to the NuttX idle trampoline.
 *
 ****************************************************************************/

static void bk7258_cpu2_boot(void)
{
  __asm__ volatile ("cpsid i" : : : "memory");

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_ENTRY);

  /* CPU2 may inherit cache state from the Bootloader/vendor AP lifecycle.
   * Match the CPU1 policy: deterministic uncached shared-SRAM path. */

  if ((getreg32(NVIC_CFGCON) & NVIC_CFGCON_DC) != 0)
    {
      up_disable_dcache();
    }

  up_enable_icache();

  /* Set up the per-CPU interrupt stack and select PSP for thread mode.  This
   * uses this_cpu() == 1, resolved from the CPU2 boot-stack MSP. */

#if CONFIG_ARCH_INTERRUPTSTACK > 7
  arm_initialize_stack();
#endif

  /* Reuse the primary-core NVIC exception setup: the vector table and the
   * shared irq_attach() table are valid on every AP core, and the NVIC
   * itself is core-local so this does not disturb CPU1. */

  up_irqinitialize();

  /* up_irqinitialize() ends by enabling interrupts.  Do not let any pending
   * NVIC line (Mailbox doorbell, or a stale shared interrupt) fire on this
   * core before the scheduler owns it: re-mask everything and let the
   * scheduler re-enable interrupts in nx_idle_trampoline() -> sched_unlock().
   */

  __asm__ volatile ("cpsid i" : : : "memory");

  /* Release the boot handshake held by up_cpu_start(): the SWAP flag word is
   * what CPU1 polls instead of a cross-core spinlock, so bring-up does not
   * depend on exclusive-monitor semantics. */

  bk7258_ap_c2flag_set();

#ifdef CONFIG_SCHED_INSTRUMENTATION
  /* Notify that this CPU has started */

  sched_note_cpu_started(this_task());
#endif

#ifdef CONFIG_BK7258_MAILBOX
  /* Enable CPU2's Mailbox line only after the boot handshake is released.
   * The Mailbox IRQ is a shared line: enabling it too early lets the CP->AP
   * RPTUN doorbell (channel 1) keep interrupting this core before the idle
   * trampoline owns it, starving the boot path.  Discard this core's stale
   * FIFO entries first: a warm reset keeps Mailbox state alive, and an old
   * SMP_MAGIC entry dispatched here would run the SMP-call path before the
   * scheduler exists. */

  bk7258_mbox_discard_local();
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_DISCARDED);
  up_prioritize_irq(BK7258_IRQ_MAILBOX,
                    CONFIG_BK7258_MAILBOX_IRQ_PRIORITY);
  up_enable_irq(BK7258_IRQ_MAILBOX);
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_IRQ_ARMED);
#endif

  /* Then transfer control to the IDLE task */

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_READY);
  nx_idle_trampoline();

  for (; ; )
    {
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_smp_ipi_handler
 *
 * Description:
 *   Mailbox IPI callback.  The Mailbox ISR demuxed an AP SMP kick (not an
 *   RPTUN doorbell) and already cleared the shared pending latch.  Service
 *   queued cross-CPU calls and tasks delivered to this core.
 *
 ****************************************************************************/

static void bk7258_smp_ipi_handler(uint8_t src_cpu, uint32_t data0,
                                   uint32_t data1, void *arg)
{
  (void)src_cpu;
  (void)data0;
  (void)data1;
  (void)arg;

  nxsched_smp_call_handler(BK7258_IRQ_MAILBOX, NULL, NULL);
  nxsched_process_delivered(this_cpu());
}

/****************************************************************************
 * Name: bk7258_smp_initialize
 *
 * Description:
 *   Arm the AP SMP doorbell before the first IPI can be issued.  Must run on
 *   the primary AP core before up_cpu_start() releases physical CPU2; the
 *   RPTUN path later calls bk7258_mbox_attach()/bk7258_mbox_init() again,
 *   which are idempotent and only fill in the RPTUN callback slot.
 *
 ****************************************************************************/

void bk7258_smp_initialize(void)
{
#ifdef CONFIG_BK7258_MAILBOX
  bk7258_mbox_attach_ipi(bk7258_smp_ipi_handler, NULL);
  (void)bk7258_mbox_init(false);
#endif
}

/****************************************************************************
 * Name: up_cpu_idlestack
 ****************************************************************************/

int up_cpu_idlestack(int cpu, FAR struct tcb_s *tcb, size_t stack_size)
{
#if CONFIG_SMP_NCPUS > 1
  up_create_stack(tcb, stack_size, TCB_FLAG_TTYPE_KERNEL);
#endif
  return OK;
}

/****************************************************************************
 * Name: up_cpu_start
 ****************************************************************************/

int up_cpu_start(int cpu)
{
  irqstate_t flags;
  uint32_t control;
  uint32_t expected;
  uint32_t status;
  unsigned int count;
  int ret = OK;

  if (cpu != 1)
    {
      return -EINVAL;
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION
  /* Notify of the start event */

  sched_note_cpu_start(this_task(), cpu);
#endif

  /* Ensure the SMP doorbell and IPI handler are live before CPU2 exists. */

  bk7258_smp_initialize();
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_C2START_ENTRY);

  flags = up_irq_save();

  /* Follow the CPU1 lifecycle order from bk7258_ap_control.c: power up and
   * unhalt CPU2 with reset asserted, program the boot vector, then release
   * reset.  The boot vector is the in-image _vectors_core1 table. */

  control = getreg32(BK7258_SYS_CPU2_CTRL);
  control &= ~(BK7258_SYS_CPU2_RESET_RELEASE |
               BK7258_SYS_CPU2_POWER_DOWN |
               BK7258_SYS_CPU2_HALT |
               BK7258_SYS_CPU2_OFFSET_MASK);
  control |= BK7258_SYS_CPU2_RXEVT_SEL;
  putreg32(control, BK7258_SYS_CPU2_CTRL);
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
  expected = control & BK7258_CPU2_CTRL_LIFECYCLE_MASK;
  if ((getreg32(BK7258_SYS_CPU2_CTRL) &
       BK7258_CPU2_CTRL_LIFECYCLE_MASK) != expected)
    {
      /* Visible instead of silent: the AP panic notifier is registered only
       * in board_late_initialize(), after nx_smp_start().  A failure here
       * would otherwise leave the CP monitor staring at reset-entered with
       * no fault record.  Low byte = the raw CTRL readback that mismatched.
       */

      bk7258_ap_record_fault(BK7258_AP_FAULT_C2START_BASE |
                             (getreg32(BK7258_SYS_CPU2_CTRL) &
                              UINT32_C(0xff)));
      ret = -EIO;
      goto out;
    }

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_C2START_CTRL1);

  for (count = 0; count < BK7258_CPU2_POWER_STABILIZE_LOOPS; count++)
    {
      __asm__ volatile ("nop");
    }

  control = getreg32(BK7258_SYS_CPU2_CTRL);
  control &= ~(BK7258_SYS_CPU2_OFFSET_MASK |
               BK7258_SYS_CPU2_RESET_RELEASE);

  /* CPU2 uses the same 256-byte boot-offset field as CPU1. */

  control |= BK7258_SYS_CPU_BOOT_OFFSET((uintptr_t)_vectors_core1);
  control |= BK7258_SYS_CPU2_RXEVT_SEL;
  putreg32(control, BK7258_SYS_CPU2_CTRL);
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
  expected = control & BK7258_CPU2_CTRL_LIFECYCLE_MASK;
  if ((getreg32(BK7258_SYS_CPU2_CTRL) &
       BK7258_CPU2_CTRL_LIFECYCLE_MASK) != expected)
    {
      bk7258_ap_record_fault(BK7258_AP_FAULT_C2START_BASE |
                             (getreg32(BK7258_SYS_CPU2_CTRL) &
                              UINT32_C(0xff)));
      ret = -EIO;
      goto out;
    }

  /* Clear the SWAP handshake flag while CPU2 is still held in reset, so a
   * stale value from an earlier CPU2 life can never satisfy the poll. */

  bk7258_ap_c2flag_clear();

  putreg32(control | BK7258_SYS_CPU2_RESET_RELEASE,
           BK7258_SYS_CPU2_CTRL);
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_C2START_RELEASED);

out:
  up_irq_restore(flags);

  if (ret < 0)
    {
      return ret;
    }

  /* Handshake on the dedicated SWAP flag word instead of a cross-core
   * spinlock: CPU1 cleared it before the release and CPU2 sets it once its
   * early boot completed.  Polling a plain SRAM word keeps bring-up
   * independent of the exclusive-monitor semantics that the runtime
   * scheduler spinlocks still need verified. */

  for (count = 0; count < BK7258_CPU2_HANDSHAKE_LOOPS; count++)
    {
      if (bk7258_ap_c2flag_test())
        {
          break;
        }
    }

  if (count == BK7258_CPU2_HANDSHAKE_LOOPS)
    {
      /* Fail loudly but stay diagnosable: the fault word carries the raw
       * CPU status and the CP monitor reports stage=fault, while nx_smp_start
       * turns -ETIMEDOUT into a recorded panic instead of a silent stall. */

      status = getreg32(BK7258_SYS_CPU_STATUS);
      bk7258_ap_record_fault(BK7258_AP_FAULT_C2START_BASE |
                             (status & UINT32_C(0xff)));
      ret = -ETIMEDOUT;
    }
  else
    {
      bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_C2START_HANDSHAKE_OK);
    }

  return ret;
}

/****************************************************************************
 * Name: up_send_smp_sched
 ****************************************************************************/

int up_send_smp_sched(int cpu)
{
  return bk7258_mbox_ipi(BK7258_AP_LOGICAL2PHYS(cpu));
}

/****************************************************************************
 * Name: up_send_smp_call
 ****************************************************************************/

void up_send_smp_call(cpu_set_t cpuset)
{
  int cpu;

  for (; cpuset != 0; cpuset &= ~(UINT32_C(1) << cpu))
    {
      cpu = ffs(cpuset) - 1;
      up_send_smp_sched(cpu);
    }
}

/****************************************************************************
 * Name: up_get_intstackbase
 ****************************************************************************/

#if CONFIG_ARCH_INTERRUPTSTACK > 7
uintptr_t up_get_intstackbase(int cpu)
{
  return (uintptr_t)g_bk7258_cpu_intstack_top[cpu] - INTSTACK_SIZE;
}
#endif

#endif /* CONFIG_SMP */

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
#include <nuttx/atomic.h>
#include <nuttx/compiler.h>
#include <nuttx/irq.h>
#include <nuttx/sched_note.h>

#include "init/init.h"

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "nvic.h"
#include "bk7258_internal.h"
#include "ram_vectors.h"
#include "chip.h"
#include "sched/sched.h"
#include "include/bk7258_ap_boot.h"
#include "include/bk7258_mailbox.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_smp.h"
#include "include/bk7258_timer.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* HALT is deliberately NOT verified, mirroring
 * BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK in board/bk7258-devkit/src/
 * bk7258_ap_control.c -- see the comment there for the full reasoning.
 *
 * Short version: the authority releases a slave core from a state where halt is
 * asserted and never waits for it to clear.  reset_cpu2_core()
 * (cp/components/bk_startup/system_main.c:205-213) writes only pwr_dw(0),
 * rxevt_sel(1), the boot offset and reset(1), with no halt write and no
 * read-back verification at all -- and its own pm_hardware_init() has already
 * set halt via sys_hal_power_config_default().
 *
 * Board evidence that requiring it breaks the release: the CP monitor reported
 *
 *   [AP] bootmark=0x00000200        C2START_ENTRY reached, CTRL1 never
 *   [AP] stage=fault fault=0x43325438
 *
 * and 0x43325438 is BK7258_AP_FAULT_C2START_BASE | 0x38, where the low byte is
 * the raw CPU2_CTRL read-back (see the fault record call below).  0x38 is
 * HALT|speed|RXEVT_SEL against an expected 0x20 -- bit1 POWER_DOWN had cleared,
 * so the power-down write did land and only halt stayed set.  Byte-for-byte the
 * same mismatch CPU1 produced before HALT came out of its mask. */

#define BK7258_CPU2_CTRL_LIFECYCLE_MASK \
  (BK7258_SYS_CPU2_RESET_RELEASE | BK7258_SYS_CPU2_POWER_DOWN | \
   BK7258_SYS_CPU2_RXEVT_SEL | BK7258_SYS_CPU2_OFFSET_MASK)

#define BK7258_CPU2_POWER_STABILIZE_LOOPS 1000

/* SWAP handshake poll bound.  The loop body is one SRAM read from XIP code,
 * so this is on the order of a second -- long enough for CPU2 to leave
 * reset and finish its early boot, short enough to fail visibly instead of
 * hanging the AP primary core. */

#define BK7258_CPU2_HANDSHAKE_LOOPS       20000000

/* Cross-core exclusive-monitor probe.  See the big comment in the probe
 * section below; these bounds and the mark codes are diagnostics only. */

#define BK7258_SMP_PROBE_ITERS            UINT32_C(4096)
#define BK7258_SMP_PROBE_PEER_WAIT_LOOPS  UINT32_C(50000000)

/* CPU1 scheduler-gate spin bound (v5).  bk7258_cpu2_boot() holds CPU2 out of
 * the shared scheduler until CPU0 finished constructing the early-board
 * threads; the bound only prevents the secondary core from spinning forever
 * when the primary fails abnormally.  CPU0 raises the flag unconditionally
 * at the end of bk7258_ap_initialize(), so on the healthy path this wait is
 * microseconds-to-milliseconds; the budget is deliberately a multiple of the
 * CPU2 handshake budget so a slow audio init cannot exhaust it. */

#define BK7258_SMP_GATE_LOOPS              (4 * BK7258_CPU2_HANDSHAKE_LOOPS)

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

/* ------------------------------------------------------------------------
 * Cross-core exclusive-monitor probe (bring-up diagnostics)
 *
 * The runtime scheduler lock on this platform is an LDREX/STREX compare-
 * exchange over shared SRAM (nuttx/include/nuttx/spinlock.h rspin_lock ->
 * atomic_cmpxchg_acquire).  That is only safe if the two AP cores' exclusive
 * accesses are arbitrated by a global monitor on the SRAM port; a part where
 * STREX always "succeeds" lets both cores believe they own the lock -- the
 * exact silent kill a0be647 recorded and the F1-F9 image still reproduces at
 * the first shared-scheduler contention (~1 ms after CPU2 READY).  Armino's
 * proven dual-core FreeRTOS AP runs per-core lists with mailbox handoffs, so
 * the silicon fabric monitor status is still unverified.
 *
 * This probe runs once per boot inside the CPU2 release window:
 *
 *   probe_reset():  CPU1, in the release critical section (fresh state).
 *   probe_cpu2():   CPU2, right after the SWAP handshake flag (waits for go).
 *   probe_run():    CPU1, right after up_irq_restore() -- sets go, runs the
 *                   same loop, waits for CPU2's done, evaluates.
 *
 * Both cores execute BK7258_SMP_PROBE_ITERS atomic_fetch_add() calls on the
 * same shared word, gated so the loops overlap by construction.  With a
 * working global monitor the final value is exactly 2 * ITERS; every lost
 * update means the monitor does not arbitrate cross-core exclusives, which
 * poisons the scheduler spinlock.
 *
 * The outcome is encoded into SWAP word 40 (bk7258_ap_dbg_mark), which the
 * CP monitor prints verbatim on change:
 *
 *   0x0f01xxxxxxxx  PASS -- xxxx = lost updates (0 on a healthy part)
 *   0x0f02xxxxxxxx  FAIL -- xxxx = lost updates
 *   0x0f03          FAIL + failsafe applied (CPU2 parked)
 *
 * On FAIL the AP continues as a single-core system: CPU2 is parked before it
 * can reach the idle trampoline, so it can never contend for the shared
 * scheduler lock, and CPU0 boots to scheduler-running exactly as the
 * a0be647-era single-core state did.  This keeps every probe flash usable
 * and gives a clean baseline for comparing SMP behavior.
 *
 * Diagnostics only: probe_reset()/probe_run()/probe_cpu2() hooks are removed
 * once SMP is resolved, leaving cpu2_boot() and up_cpu_start() unchanged.
 * ------------------------------------------------------------------------ */

static atomic_t g_smp_probe_go;
static atomic_t g_smp_probe_fadd;
static atomic_t g_smp_probe_c2done;

static void bk7258_smp_probe_reset(void)
{
  atomic_set(&g_smp_probe_go, 0);
  atomic_set(&g_smp_probe_fadd, 0);
  atomic_set(&g_smp_probe_c2done, 0);
  __asm__ volatile ("dsb" : : : "memory");
}

static void bk7258_smp_probe_cpu2(void)
{
  uint32_t i;

  /* Gate on CPU1's go flag so CPU1's loop and this loop overlap.  The wait
   * is bounded; CPU1 always sets go unconditionally right after the release,
   * so this only spins for the few microseconds of the out: restore. */

  for (i = 0; i < BK7258_SMP_PROBE_PEER_WAIT_LOOPS; i++)
    {
      if (atomic_read(&g_smp_probe_go) != 0)
        {
          break;
        }
    }

  for (i = 0; i < BK7258_SMP_PROBE_ITERS; i++)
    {
      (void)atomic_add(&g_smp_probe_fadd, 1);
    }

  atomic_set(&g_smp_probe_c2done, 1);
  __asm__ volatile ("dsb" : : : "memory");
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_SMP_PROBE_C2DONE);
}

static void bk7258_smp_park_cpu2(void)
{
  uint32_t control;

  /* Power down and halt CPU2 and drop its reset-release, so it cannot run
   * again this session.  Same authority as the release in up_cpu_start(). */

  control = getreg32(BK7258_SYS_CPU2_CTRL);
  control &= ~(BK7258_SYS_CPU2_RESET_RELEASE |
               BK7258_SYS_CPU2_RXEVT_SEL |
               BK7258_SYS_CPU2_OFFSET_MASK);
  control |= BK7258_SYS_CPU2_POWER_DOWN | BK7258_SYS_CPU2_HALT;
  putreg32(control, BK7258_SYS_CPU2_CTRL);
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
}

static void bk7258_smp_probe_run(void)
{
  uint32_t done;
  uint32_t i;
  uint32_t loss;

  atomic_set(&g_smp_probe_go, 1);
  __asm__ volatile ("dsb" : : : "memory");

  for (i = 0; i < BK7258_SMP_PROBE_ITERS; i++)
    {
      (void)atomic_add(&g_smp_probe_fadd, 1);
    }

  for (i = 0; i < BK7258_SMP_PROBE_PEER_WAIT_LOOPS; i++)
    {
      done = atomic_read(&g_smp_probe_c2done);
      if (done != 0)
        {
          break;
        }
    }

  loss = (2 * BK7258_SMP_PROBE_ITERS) - atomic_read(&g_smp_probe_fadd);

  if (loss == 0)
    {
      bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_SMP_PROBE_PASS);
      return;
    }

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_SMP_PROBE_FAIL |
                     ((loss & UINT32_C(0xffff)) << 16));

  /* Never let a failed probe steal the whole board: park CPU2 and let the
   * AP continue single-core. */

  bk7258_smp_park_cpu2();
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_SMP_PROBE_FAILSAFE);
}

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

  /* Same MPU + D-cache policy as CPU1: program the shared region table
   * (SRAM non-cacheable, flash/PSRAM write-back) and turn the D-cache on
   * now that the shared windows cannot go stale.  Registers are per-core,
   * so this does not disturb the CPU1 MPU. */

#ifdef CONFIG_ARM_MPU
  bk7258_mpuinit();
#endif

#if defined(CONFIG_ARM_MPU) && defined(CONFIG_ARMV8M_DCACHE)
  up_enable_dcache();
#endif

  /* Set up the per-CPU interrupt stack and select PSP for thread mode.  This
   * uses this_cpu() == 1, resolved from the CPU2 boot-stack MSP. */

#if CONFIG_ARCH_INTERRUPTSTACK > 7
  arm_initialize_stack();
#endif

  /* Reuse the primary-core NVIC exception setup: the vector table and the
   * shared irq_attach() table are valid on every AP core, and the NVIC
   * itself is core-local so this does not disturb CPU1. */

  up_irqinitialize();

  /* up_irqinitialize() ends by enabling interrupts, and they must STAY
   * enabled through nx_idle_trampoline().  NuttX's ARMv8-M critical
   * sections save/restore BASEPRI only (arch/arm/include/arm_m/irq.h:
   * up_irq_save()/up_irq_restore() never touch PRIMASK), and
   * nx_idle_trampoline() -> sched_unlock() never calls up_irq_enable().
   * A PRIMASK set here (cpsid i) could therefore never be cleared again:
   * the Mailbox IPI would stay pended in the NVIC forever, an SVC-based
   * context switch (priority 0x60) would escalate to HardFault, and this
   * core's own SysTick would be dead -- the exact PRIMASK trap that
   * d009584 warned about.  Stale pending lines are handled below instead:
   * the Mailbox channel is drained and its NVIC pending bit cleared
   * before up_enable_irq() arms it.
   */

  /* Release the boot handshake held by up_cpu_start(): the SWAP flag word is
   * what CPU1 polls instead of a cross-core spinlock, so bring-up does not
   * depend on exclusive-monitor semantics. */

  bk7258_ap_c2flag_set();

  /* Cross-core exclusive-monitor probe (diagnostics; removed after SMP is
   * resolved).  Runs before the mailbox/timer path so a parked CPU2 on the
   * failed-monitor fallback can never arm the tick or reach the trampoline. */

  bk7258_smp_probe_cpu2();

  /* CPU1 scheduler gate (v5): hold CPU2 out of the shared scheduler until
   * CPU0 finished constructing the early-board threads (audio sync init
   * plus amp-init/ap-health kthread creation inside bk7258_ap_initialize()).
   * nx_idle_trampoline()'s own while (!OSINIT_IDLELOOP()) cannot cover this
   * window: the primary raises OSINIT_IDLELOOP right after nx_bringup()
   * spawns AppBringUp, i.e. before that thread has even run
   * board_late_initialize().  With neither the Mailbox IPI nor SysTick
   * armed, this core has no path into nxsched_process_delivered() or
   * nxsched_process_timer(); the gate therefore serializes CPU2's first
   * shared-scheduler contact until CPU0's early heap objects are fully
   * constructed -- the exact window the v4j sched-trace proved both cores
   * were climbing concurrently a few milliseconds after the release.
   * Bounded spin: on timeout we degrade to the pre-fix behavior instead of
   * hanging the secondary core.  Runs after probe_cpu2() on purpose: CPU0's
   * probe_run() waits for the c2done latch, so gating the probe itself
   * would trip the failsafe and park this core. */

  {
    uint32_t gate_count;

    bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_GATE_WAIT);

    for (gate_count = 0; gate_count < BK7258_SMP_GATE_LOOPS; gate_count++)
      {
        if (bk7258_ap_sched_gate_test())
          {
            break;
          }
      }

    if (gate_count < BK7258_SMP_GATE_LOOPS)
      {
        bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_GATE_OK);
      }
    else
      {
        bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_CPU2_GATE_TIMEOUT);
      }
  }

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

  /* Arm this core's system tick before entering the idle trampoline.  The
   * SysTick registers are per-core and the systick_interrupt handler was
   * attached by the primary core's up_timer_initialize(); every tick runs
   * nxsched_process_timer(), whose paths are SMP-safe.  The per-CPU idle
   * TCB and scheduler lists already exist here (idle_task_initialize()
   * runs long before up_cpu_start()), and nx_idle_trampoline() spins on
   * OSINIT_IDLELOOP() with interrupts enabled until CPU0 enters its idle
   * loop, so early ticks land in a fully initialized scheduler. */

  bk7258_cpu2_timer_initialize();

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

  /* Fresh probe state before CPU2 can touch it (diagnostics; removed after
   * SMP is resolved). */

  bk7258_smp_probe_reset();

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

  /* Clear the CPU1 scheduler gate the same way: a stale GATE word from an
   * earlier CPU2 life would let this boot's CPU2 skip the v5 serializer and
   * re-open the early-double-schedule race the gate exists to close. */

  bk7258_ap_sched_gate_clear();

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

  /* Cross-core exclusive-monitor probe (diagnostics; removed after SMP is
   * resolved).  Gates on the CPU2 loop, evaluates, and parks CPU2 on a
   * failed monitor so the AP still boots single-core instead of deadlocking
   * at the first shared-scheduler contention. */

  bk7258_smp_probe_run();

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

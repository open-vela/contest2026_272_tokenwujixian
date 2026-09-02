/****************************************************************************
 * board/bk7258-devkit/src/bk7258_ap_control.c
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_COMPONENT_CP

#include <errno.h>
#include <stdint.h>
#include <syslog.h>
#include <sys/boardctl.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/irq.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/rptun/rptun.h>
#include <nuttx/signal.h>

#include <arch/irq.h>
#include <arch/chip/bk7258_ap_boot.h>
#include <arch/chip/bk7258_memorymap.h>

#define BK7258_AP_MONITOR_PRIORITY       100
#define BK7258_AP_MONITOR_STACK_SIZE     2048
#define BK7258_AP_STALL_SAMPLES          3
#define BK7258_AP_POWER_STABILIZE_LOOPS  1000
#define BK7258_AP_POWER_WAIT_LOOPS       10000
#define BK7258_AP_RESET_WAIT_ATTEMPTS    50
#define BK7258_AP_RESET_WAIT_USEC        (10 * 1000)
#define BK7258_AP_RPTUN_STOP_ATTEMPTS    50
#define BK7258_AP_RPTUN_STOP_WAIT_USEC   (10 * 1000)

#define BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK \
  (BK7258_SYS_CPU1_RESET_RELEASE | BK7258_SYS_CPU1_POWER_DOWN | \
   BK7258_SYS_CPU1_HALT | BK7258_SYS_CPU1_RXEVT_SEL | \
   BK7258_SYS_CPU1_OFFSET_MASK)

static uint32_t g_ap_boot_sequence;
static mutex_t g_ap_start_lock = NXMUTEX_INITIALIZER;
static bool g_ap_started;
static bool g_ap_recovering;
static bool g_ap_monitor_started;

static inline uint32_t bk7258_ap_reg_read(uintptr_t address)
{
  return *(volatile uint32_t *)address;
}

static inline void bk7258_ap_reg_write(uintptr_t address, uint32_t value)
{
  *(volatile uint32_t *)address = value;
}

static inline void bk7258_ap_lifecycle_barrier(void)
{
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
}

struct bk7258_ap_audio_wake_s
{
  uint32_t power_before;
  uint32_t power_after;
  uint32_t mem_ctrl1_before;
  uint32_t mem_ctrl1_after;
  uint32_t mem_ctrl2_before;
  uint32_t mem_ctrl2_after;
};

/* CP owns these shared SYS controls while CPU1 is held in reset.  Keep the
 * three whole-word RMW operations in that exclusive window, then verify the
 * hardware-visible values before AP can execute any audio initialization.
 */

static int bk7258_ap_audio_wake(
  struct bk7258_ap_audio_wake_s *state)
{
  state->power_before =
    bk7258_ap_reg_read(BK7258_SYS_POWER_WAKEUP);
  state->mem_ctrl1_before =
    bk7258_ap_reg_read(BK7258_SYS_DEV_MEM_CTRL1);
  state->mem_ctrl2_before =
    bk7258_ap_reg_read(BK7258_SYS_DEV_MEM_CTRL2);

  bk7258_ap_reg_write(BK7258_SYS_POWER_WAKEUP,
                      state->power_before &
                      ~BK7258_SYS_AUDP_POWER_DOWN);
  bk7258_ap_reg_write(BK7258_SYS_DEV_MEM_CTRL1,
                      state->mem_ctrl1_before &
                      ~BK7258_SYS_AUD_MEM_LOW_POWER);
  bk7258_ap_reg_write(BK7258_SYS_DEV_MEM_CTRL2,
                      state->mem_ctrl2_before &
                      ~BK7258_SYS_AUD_MEM_LOW_POWER);
  bk7258_ap_lifecycle_barrier();
  up_udelay(10);

  state->power_after =
    bk7258_ap_reg_read(BK7258_SYS_POWER_WAKEUP);
  state->mem_ctrl1_after =
    bk7258_ap_reg_read(BK7258_SYS_DEV_MEM_CTRL1);
  state->mem_ctrl2_after =
    bk7258_ap_reg_read(BK7258_SYS_DEV_MEM_CTRL2);

  if ((state->power_after & BK7258_SYS_AUDP_POWER_DOWN) != 0 ||
      (state->mem_ctrl1_after & BK7258_SYS_AUD_MEM_LOW_POWER) != 0 ||
      (state->mem_ctrl2_after & BK7258_SYS_AUD_MEM_LOW_POWER) != 0)
    {
      return -EIO;
    }

  return 0;
}

static const char *bk7258_ap_stage_name(uint32_t stage)
{
  switch (stage)
    {
      case BK7258_AP_STAGE_CP_PREPARED:
        return "prepared";
      case BK7258_AP_STAGE_RESET_ENTERED:
        return "reset-entered";
      case BK7258_AP_STAGE_SCHEDULER_RUNNING:
        return "scheduler-running";
      case BK7258_AP_STAGE_FAULT:
        return "fault";
      default:
        return "unknown";
    }
}

static int bk7258_ap_force_reset(void)
{
  irqstate_t flags;
  uint32_t control;

  flags = up_irq_save();
  control = bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL);
  control &= ~BK7258_SYS_CPU1_RESET_RELEASE;
  bk7258_ap_reg_write(BK7258_SYS_CPU1_CTRL, control);
  bk7258_ap_lifecycle_barrier();
  if ((bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL) &
       BK7258_SYS_CPU1_RESET_RELEASE) != 0)
    {
      up_irq_restore(flags);
      return -EIO;
    }

  up_irq_restore(flags);
  return 0;
}

int bk7258_ap_reset_for_rptun(void)
{
  int ret;

  ret = nxmutex_lock(&g_ap_start_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ap_force_reset();
  if (ret >= 0)
    {
      g_ap_started = false;
    }

  nxmutex_unlock(&g_ap_start_lock);
  return ret;
}

int board_start_cpu(int cpuid)
{
  struct bk7258_ap_audio_wake_s audio_wake = {0};
  struct bk7258_ap_boot_record_s record;
  irqstate_t flags;
  uint32_t control;
  uint32_t expected;
  uint32_t initial_msp;
  uint32_t reset_pc;
  uint32_t status;
  const volatile struct bk7258_ap_image_contract_s *image_contract;
  uint32_t boot_sequence = 0;
  unsigned int count;
  bool audio_wake_attempted = false;
  bool released = false;
  int audio_wake_ret = 0;
  int ret;

  if (cpuid != 1)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_ap_start_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_ap_started)
    {
      ret = -EBUSY;
      goto out_unlock;
    }

  /* Fail closed before trusting any AP partition bytes. This also makes the
   * default placeholder/recovery package safe if a prior stage left CPU1
   * running: CPU1 is parked before OVAP identity or vector validation. */

  ret = bk7258_ap_force_reset();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[AP] start: force_reset failed ret=%d\n", ret);
      goto out_unlock;
    }

  image_contract =
    (const volatile struct bk7258_ap_image_contract_s *)
      (BK7258_AP_XIP_VECTOR_BASE + BK7258_AP_IMAGE_CONTRACT_OFFSET);
  syslog(LOG_INFO,
         "[AP] start: contract@%p magic=%08lx abi=%lu build=%08lx "
         "vbase=%08lx (want magic=%08lx vbase=%08lx)\n",
         (const void *)image_contract,
         (unsigned long)image_contract->magic,
         (unsigned long)image_contract->abi_version,
         (unsigned long)image_contract->build_id,
         (unsigned long)image_contract->vector_base,
         (unsigned long)BK7258_AP_IMAGE_MAGIC,
         (unsigned long)BK7258_AP_XIP_VECTOR_BASE);
  if (image_contract->magic != BK7258_AP_IMAGE_MAGIC ||
      image_contract->abi_version != BK7258_AP_BOOT_ABI_VERSION ||
      image_contract->build_id != BK7258_AP_BUILD_ID ||
      image_contract->vector_base != BK7258_AP_XIP_VECTOR_BASE)
    {
      syslog(LOG_ERR, "[AP] start: contract mismatch -> ENOEXEC\n");
      ret = -ENOEXEC;
      goto out_unlock;
    }

  initial_msp = bk7258_ap_reg_read(BK7258_AP_XIP_VECTOR_BASE);
  reset_pc = bk7258_ap_reg_read(BK7258_AP_XIP_VECTOR_BASE + sizeof(uint32_t));
  syslog(LOG_INFO,
         "[AP] start: vbase=%08lx msp=%08lx reset_pc=%08lx "
         "(ram=[%08lx..%08lx] xip_end=%08lx)\n",
         (unsigned long)BK7258_AP_XIP_VECTOR_BASE,
         (unsigned long)initial_msp, (unsigned long)reset_pc,
         (unsigned long)BK7258_AP_RAM_BASE, (unsigned long)BK7258_AP_RAM_END,
         (unsigned long)(BK7258_AP_XIP_VECTOR_BASE + BK7258_AP_XIP_SIZE));
  if (initial_msp <= BK7258_AP_RAM_BASE ||
      initial_msp > BK7258_AP_RAM_END ||
      (initial_msp & 7) != 0 ||
      (reset_pc & 1) == 0 ||
      (reset_pc & ~UINT32_C(1)) < BK7258_AP_XIP_VECTOR_BASE ||
      (reset_pc & ~UINT32_C(1)) >=
        BK7258_AP_XIP_VECTOR_BASE + BK7258_AP_XIP_SIZE)
    {
      syslog(LOG_ERR, "[AP] start: vector bounds check failed -> ENOEXEC\n");
      ret = -ENOEXEC;
      goto out_unlock;
    }

  flags = up_irq_save();

  /* Follow Armino's BK7258 lifecycle order. CPU_CURRENT_RUN_STATUS is
   * diagnostic only; its transient values are not a launch precondition. */

  control = bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL);
  syslog(LOG_INFO, "[AP] start: cpu1_ctrl before=%08lx\n",
         (unsigned long)control);
  control &= ~(BK7258_SYS_CPU1_RESET_RELEASE |
               BK7258_SYS_CPU1_POWER_DOWN |
               BK7258_SYS_CPU1_HALT |
               BK7258_SYS_CPU1_OFFSET_MASK);
  control |= BK7258_SYS_CPU1_RXEVT_SEL;
  bk7258_ap_reg_write(BK7258_SYS_CPU1_CTRL, control);
  bk7258_ap_lifecycle_barrier();
  expected = control & BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK;
  if ((bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL) &
       BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK) != expected)
    {
      syslog(LOG_ERR,
             "[AP] start: cpu1_ctrl readback=%08lx expected=%08lx -> EIO\n",
             (unsigned long)bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL),
             (unsigned long)expected);
      up_irq_restore(flags);
      ret = -EIO;
      goto out_unlock;
    }

  /* Keep the vendor power-domain settling window explicit and
   * non-optimizable. */

  for (count = 0; count < BK7258_AP_POWER_STABILIZE_LOOPS; count++)
    {
      __asm__ volatile ("nop");
    }

  for (count = 0; count < BK7258_AP_POWER_WAIT_LOOPS; count++)
    {
      status = bk7258_ap_reg_read(BK7258_SYS_CPU_STATUS);
      if ((status & (BK7258_SYS_CPU1_PWR_DW_STATE |
                     BK7258_SYS_CPU1_HALTED_STATE |
                     BK7258_SYS_CPU1_RESET_STATE)) == 0)
        {
          break;
        }
    }

  if (count == BK7258_AP_POWER_WAIT_LOOPS)
    {
      syslog(LOG_ERR,
             "[AP] start: power-wait timeout cpu_status=%08lx -> ETIMEDOUT\n",
             (unsigned long)bk7258_ap_reg_read(BK7258_SYS_CPU_STATUS));
      up_irq_restore(flags);
      ret = -ETIMEDOUT;
      goto out_unlock;
    }

  syslog(LOG_INFO, "[AP] start: cpu1 powered up, cpu_status=%08lx after %u\n",
         (unsigned long)bk7258_ap_reg_read(BK7258_SYS_CPU_STATUS),
         (unsigned int)count);

  /* CPU1 is still held in reset and CP interrupts remain disabled, so this
   * is the exclusive ownership window for the shared audio power/SRAM RMWs.
   * A failed readback keeps AP in reset and prevents an unusable audio
   * subsystem from being published.
   */

  audio_wake_attempted = true;
  audio_wake_ret = bk7258_ap_audio_wake(&audio_wake);
  if (audio_wake_ret < 0)
    {
      up_irq_restore(flags);
      ret = audio_wake_ret;
      goto out_unlock;
    }

  /* CPU1 is now held in reset, so CP owns SWAP exclusively while it creates
   * the next boot sequence. This ordering also makes a future AP restart safe:
   * the old AP cannot race the new CP record with a final heartbeat write. */

  boot_sequence = bk7258_ap_record_prepare();

  control = bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL);
  control &= ~(BK7258_SYS_CPU1_OFFSET_MASK |
               BK7258_SYS_CPU1_RESET_RELEASE);
  /* The Armino setter takes address >> 8 as a bitfield value. This direct
   * register write must place that value back into bits [31:8]. */

  control |= BK7258_SYS_CPU_BOOT_OFFSET(BK7258_AP_XIP_VECTOR_BASE);
  control |= BK7258_SYS_CPU1_RXEVT_SEL;
  bk7258_ap_reg_write(BK7258_SYS_CPU1_CTRL, control);
  bk7258_ap_lifecycle_barrier();
  expected = control & BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK;
  if ((bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL) &
       BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK) != expected)
    {
      syslog(LOG_ERR,
             "[AP] start: vec-offset readback=%08lx expected=%08lx -> EIO\n",
             (unsigned long)bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL),
             (unsigned long)expected);
      up_irq_restore(flags);
      ret = -EIO;
      goto out_unlock;
    }

  syslog(LOG_INFO, "[AP] start: releasing CPU1 reset (seq=%u)\n",
         (unsigned int)boot_sequence);
  bk7258_ap_reg_write(BK7258_SYS_CPU1_CTRL,
                      control | BK7258_SYS_CPU1_RESET_RELEASE);
  released = true;
  bk7258_ap_lifecycle_barrier();
  expected = (control | BK7258_SYS_CPU1_RESET_RELEASE) &
             BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK;
  if ((bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL) &
       BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK) != expected)
    {
      syslog(LOG_ERR,
             "[AP] start: release readback=%08lx expected=%08lx -> EIO\n",
             (unsigned long)bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL),
             (unsigned long)expected);
      up_irq_restore(flags);
      ret = -EIO;
      goto out_rollback;
    }

  for (count = 0; count < BK7258_AP_POWER_WAIT_LOOPS; count++)
    {
      status = bk7258_ap_reg_read(BK7258_SYS_CPU_STATUS);
      if ((status & BK7258_SYS_CPU1_RESET_STATE) != 0)
        {
          break;
        }
    }

  if (count == BK7258_AP_POWER_WAIT_LOOPS)
    {
      syslog(LOG_ERR,
             "[AP] start: post-release reset-state timeout "
             "cpu_status=%08lx -> ETIMEDOUT\n",
             (unsigned long)bk7258_ap_reg_read(BK7258_SYS_CPU_STATUS));
      up_irq_restore(flags);
      ret = -ETIMEDOUT;
      goto out_rollback;
    }

  up_irq_restore(flags);

  /* Do not report a successful start merely because MMIO writes completed.
   * The current-sequence RESET_ENTERED record proves CPU1 fetched the AP
   * vector and executed team OpenVela startup code. */

  for (count = 0; count < BK7258_AP_RESET_WAIT_ATTEMPTS; count++)
    {
      if (bk7258_ap_record_read(&record) &&
          record.boot_sequence == boot_sequence)
        {
          if (record.stage == BK7258_AP_STAGE_RESET_ENTERED ||
              record.stage == BK7258_AP_STAGE_SCHEDULER_RUNNING)
            {
              g_ap_boot_sequence = boot_sequence;
              g_ap_started = true;
              syslog(LOG_INFO,
                     "[AP] start: OK stage=%s seq=%u after %u\n",
                     bk7258_ap_stage_name(record.stage),
                     (unsigned int)boot_sequence,
                     (unsigned int)count);
              ret = 0;
              goto out_unlock;
            }

          if (record.stage == BK7258_AP_STAGE_FAULT)
            {
              syslog(LOG_ERR,
                     "[AP] start fault seq=%u code=0x%08x\n",
                     (unsigned int)boot_sequence,
                     (unsigned int)record.fault);
              ret = -EFAULT;
              goto out_rollback;
            }
        }

      nxsig_usleep(BK7258_AP_RESET_WAIT_USEC);
    }

  syslog(LOG_ERR,
         "[AP] start: boot-record wait timeout seq=%u attempts=%u "
         "(last valid=%d seq=%u stage=%u) -> ETIMEDOUT\n",
         (unsigned int)boot_sequence,
         (unsigned int)BK7258_AP_RESET_WAIT_ATTEMPTS,
         (int)bk7258_ap_record_read(&record),
         (unsigned int)record.boot_sequence,
         (unsigned int)record.stage);
  ret = -ETIMEDOUT;

out_rollback:
  if (released)
    {
      int reset_ret = bk7258_ap_force_reset();
      if (reset_ret < 0)
        {
          syslog(LOG_ERR, "[AP] rollback reset failed: %d\n", reset_ret);
        }
    }

out_unlock:
  if (audio_wake_attempted)
    {
      syslog(audio_wake_ret < 0 ? LOG_ERR : LOG_INFO,
             "[AP] audio wake ret=%d pwr=%08lx->%08lx "
             "sd=%08lx->%08lx ds=%08lx->%08lx\n",
             audio_wake_ret,
             (unsigned long)audio_wake.power_before,
             (unsigned long)audio_wake.power_after,
             (unsigned long)audio_wake.mem_ctrl1_before,
             (unsigned long)audio_wake.mem_ctrl1_after,
             (unsigned long)audio_wake.mem_ctrl2_before,
             (unsigned long)audio_wake.mem_ctrl2_after);
    }

  nxmutex_unlock(&g_ap_start_lock);
  return ret;
}

static int bk7258_ap_recover_transport(void)
{
  unsigned int attempt;
  int ret;

  if (g_ap_recovering)
    {
      return -EALREADY;
    }

  g_ap_recovering = true;
  g_ap_started = false;

  /* The generic RPTUN stop path destroys the remoteproc VirtIO device and
   * broadcasts RPMsg device-destroy callbacks before returning the transport
   * to OFFLINE.  Do this before board_start_cpu() resets AP again, otherwise
   * the old vrings and uart_rpmsg endpoint can survive into the new AP
   * generation. */

  /* remoteproc_stop() rejects CONFIGURED/READY while the RPTUN worker is
   * still completing its start transaction.  AP can reboot exactly during
   * that window, so treat -EBUSY as a transient state and retry in this
   * task context.  Do not call rptun_initialize() here: the /dev/rptun/ap
   * instance is already registered and initialization is one-shot. */

  ret = -EBUSY;
  for (attempt = 0; attempt < BK7258_AP_RPTUN_STOP_ATTEMPTS; attempt++)
    {
      ret = rptun_poweroff("ap");
      if (ret != -EBUSY)
        {
          break;
        }

      nxsig_usleep(BK7258_AP_RPTUN_STOP_WAIT_USEC);
    }

  if (ret == -EBUSY)
    {
      syslog(LOG_ERR,
             "[AP] RPTUN teardown remained busy after %u attempts\n",
             BK7258_AP_RPTUN_STOP_ATTEMPTS);
    }

  if (ret < 0 && ret != -ENODEV)
    {
      syslog(LOG_ERR, "[AP] RPTUN teardown failed: %d\n", ret);
      goto out;
    }

  ret = rptun_boot("ap");
  if (ret < 0)
    {
      syslog(LOG_ERR, "[AP] RPTUN restart failed: %d\n", ret);
    }

out:
  g_ap_recovering = false;
  return ret;
}

static int bk7258_ap_monitor(int argc, char *argv[])
{
  struct bk7258_ap_boot_record_s record;
  uint32_t last_heartbeat = 0;
  uint32_t last_stage = UINT32_MAX;
  unsigned int invalid_samples = 0;
  unsigned int stagnant_samples = 0;
  bool invalid_reported = false;
  bool stalled_reported = false;
  bool heartbeat_seen = false;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      if (!bk7258_ap_record_read(&record) ||
          (record.boot_sequence != g_ap_boot_sequence &&
           !g_ap_recovering))
        {
          if (bk7258_ap_record_read(&record) &&
              record.boot_sequence != g_ap_boot_sequence &&
              record.stage == BK7258_AP_STAGE_SCHEDULER_RUNNING)
            {
              syslog(LOG_INFO,
                     "[AP] peer generation changed old=%u new=%u; rebuilding RPTUN\n",
                     (unsigned int)g_ap_boot_sequence,
                     (unsigned int)record.boot_sequence);
              (void)bk7258_ap_recover_transport();
              nxsig_usleep(1000 * 1000);
              continue;
            }

          invalid_samples++;
          stagnant_samples = 0;
          if (invalid_samples >= BK7258_AP_STALL_SAMPLES && !invalid_reported)
            {
              syslog(LOG_ERR,
                     "[AP] no valid record for seq=%u\n",
                     (unsigned int)g_ap_boot_sequence);
              invalid_reported = true;
            }

          nxsig_usleep(1000 * 1000);
          continue;
        }

      invalid_samples = 0;
      invalid_reported = false;

      if (record.stage != last_stage)
        {
          if (record.stage == BK7258_AP_STAGE_FAULT &&
              (record.fault & UINT32_C(0xffff0000)) ==
              (BK7258_AP_FAULT_PANIC_BASE & UINT32_C(0xffff0000)))
            {
              /* The AP panic notifier encodes the assertion location:
               * high byte = a short hash of the filename, low byte = line
               * number. */

              syslog(LOG_ERR,
                     "[AP] stage=fault seq=%u panic filehash=0x%02x line=%u\n",
                     (unsigned int)record.boot_sequence,
                     (unsigned int)((record.fault >> 8) & UINT32_C(0xff)),
                     (unsigned int)(record.fault & UINT32_C(0xff)));
            }
          else
            {
              syslog(record.stage == BK7258_AP_STAGE_FAULT ? LOG_ERR : LOG_INFO,
                     "[AP] stage=%s seq=%u fault=0x%08x\n",
                     bk7258_ap_stage_name(record.stage),
                     (unsigned int)record.boot_sequence,
                     (unsigned int)record.fault);
            }

          last_stage = record.stage;
        }

      if (record.stage == BK7258_AP_STAGE_SCHEDULER_RUNNING)
        {
          if (record.heartbeat != last_heartbeat)
            {
              stagnant_samples = 0;
              stalled_reported = false;
              last_heartbeat = record.heartbeat;
              heartbeat_seen = true;
            }
          else if (heartbeat_seen)
            {
              stagnant_samples++;
              if (stagnant_samples >= BK7258_AP_STALL_SAMPLES &&
                  !stalled_reported)
                {
                  syslog(LOG_ERR,
                         "[AP] stalled seq=%u heartbeat=%u samples=%u\n",
                         (unsigned int)record.boot_sequence,
                         (unsigned int)record.heartbeat,
                         stagnant_samples);
                  stalled_reported = true;
                }
            }
        }

      nxsig_usleep(1000 * 1000);
    }

  return 0;
}

int bk7258_ap_start_monitor(void)
{
  int ret;

  ret = boardctl(BOARDIOC_START_CPU, 1);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[AP] CPU1 release failed: %d errno=%d\n",
             ret, get_errno());
      return ret;
    }

  syslog(LOG_INFO, "[AP] release seq=%u vector=0x%08x\n",
         (unsigned int)g_ap_boot_sequence,
         (unsigned int)BK7258_AP_XIP_VECTOR_BASE);

  /* RPTUN restart reuses the long-lived CP monitor. Do not create a second
   * monitor thread when the generic RPTUN start path rebuilds a transport. */

  if (g_ap_monitor_started)
    {
      return 0;
    }

  ret = kthread_create("ap-monitor", BK7258_AP_MONITOR_PRIORITY,
                       BK7258_AP_MONITOR_STACK_SIZE,
                       bk7258_ap_monitor, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[AP] monitor creation failed: %d\n", ret);
      nxmutex_lock(&g_ap_start_lock);
      (void)bk7258_ap_force_reset();
      g_ap_started = false;
      nxmutex_unlock(&g_ap_start_lock);
      return ret;
    }

  return 0;
}

#endif /* CONFIG_BK7258_COMPONENT_CP */

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

#define BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK \
  (BK7258_SYS_CPU1_RESET_RELEASE | BK7258_SYS_CPU1_POWER_DOWN | \
   BK7258_SYS_CPU1_HALT | BK7258_SYS_CPU1_RXEVT_SEL | \
   BK7258_SYS_CPU1_OFFSET_MASK)

static uint32_t g_ap_boot_sequence;
static mutex_t g_ap_start_lock = NXMUTEX_INITIALIZER;
static bool g_ap_started;

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
  uint32_t status;
  unsigned int count;

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

  for (count = 0; count < BK7258_AP_POWER_WAIT_LOOPS; count++)
    {
      status = bk7258_ap_reg_read(BK7258_SYS_CPU_STATUS);
      if ((status & BK7258_SYS_CPU1_RESET_STATE) == 0)
        {
          up_irq_restore(flags);
          return 0;
        }
    }

  up_irq_restore(flags);
  return -ETIMEDOUT;
}

int board_start_cpu(int cpuid)
{
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
  bool released = false;
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
      goto out_unlock;
    }

  image_contract =
    (const volatile struct bk7258_ap_image_contract_s *)
      (BK7258_AP_XIP_VECTOR_BASE + BK7258_AP_IMAGE_CONTRACT_OFFSET);
  if (image_contract->magic != BK7258_AP_IMAGE_MAGIC ||
      image_contract->abi_version != BK7258_AP_BOOT_ABI_VERSION ||
      image_contract->build_id != BK7258_AP_BUILD_ID ||
      image_contract->vector_base != BK7258_AP_XIP_VECTOR_BASE)
    {
      ret = -ENOEXEC;
      goto out_unlock;
    }

  initial_msp = bk7258_ap_reg_read(BK7258_AP_XIP_VECTOR_BASE);
  reset_pc = bk7258_ap_reg_read(BK7258_AP_XIP_VECTOR_BASE + sizeof(uint32_t));
  if (initial_msp <= BK7258_AP_RAM_BASE ||
      initial_msp > BK7258_AP_RAM_END ||
      (initial_msp & 7) != 0 ||
      (reset_pc & 1) == 0 ||
      (reset_pc & ~UINT32_C(1)) < BK7258_AP_XIP_VECTOR_BASE ||
      (reset_pc & ~UINT32_C(1)) >=
        BK7258_AP_XIP_VECTOR_BASE + BK7258_AP_XIP_SIZE)
    {
      ret = -ENOEXEC;
      goto out_unlock;
    }

  flags = up_irq_save();

  /* Follow the source-backed BK7258 lifecycle order while making the reset
   * boundary explicit: power up and unhalt CPU1 with reset asserted, wait for
   * the observable state to settle, program the vector offset, then release
   * reset.  CPU2 is never touched by this MVP. */

  control = bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL);
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
      up_irq_restore(flags);
      ret = -EIO;
      goto out_unlock;
    }

  /* Keep the vendor power-domain settling window explicit and
   * non-optimizable. The bounded state poll below remains the completion
   * check; this delay is not itself treated as proof of power-up. */

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
      up_irq_restore(flags);
      ret = -ETIMEDOUT;
      goto out_unlock;
    }

  /* CPU1 is now held in reset, so CP owns SWAP exclusively while it creates
   * the next boot sequence. This ordering also makes a future AP restart safe:
   * the old AP cannot race the new CP record with a final heartbeat write. */

  boot_sequence = bk7258_ap_record_prepare();

  control = bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL);
  control &= ~(BK7258_SYS_CPU1_OFFSET_MASK |
               BK7258_SYS_CPU1_RESET_RELEASE);
  control |= BK7258_AP_XIP_VECTOR_BASE & BK7258_SYS_CPU1_OFFSET_MASK;
  control |= BK7258_SYS_CPU1_RXEVT_SEL;
  bk7258_ap_reg_write(BK7258_SYS_CPU1_CTRL, control);
  bk7258_ap_lifecycle_barrier();
  expected = control & BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK;
  if ((bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL) &
       BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK) != expected)
    {
      up_irq_restore(flags);
      ret = -EIO;
      goto out_unlock;
    }

  bk7258_ap_reg_write(BK7258_SYS_CPU1_CTRL,
                      control | BK7258_SYS_CPU1_RESET_RELEASE);
  released = true;
  bk7258_ap_lifecycle_barrier();
  expected = (control | BK7258_SYS_CPU1_RESET_RELEASE) &
             BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK;
  if ((bk7258_ap_reg_read(BK7258_SYS_CPU1_CTRL) &
       BK7258_AP_CPU1_CTRL_LIFECYCLE_MASK) != expected)
    {
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
  nxmutex_unlock(&g_ap_start_lock);
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
          record.boot_sequence != g_ap_boot_sequence)
        {
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
          syslog(record.stage == BK7258_AP_STAGE_FAULT ? LOG_ERR : LOG_INFO,
                 "[AP] stage=%s seq=%u fault=0x%08x\n",
                 bk7258_ap_stage_name(record.stage),
                 (unsigned int)record.boot_sequence,
                 (unsigned int)record.fault);
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
      syslog(LOG_ERR, "[AP] CPU1 release failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "[AP] release seq=%u vector=0x%08x\n",
         (unsigned int)g_ap_boot_sequence,
         (unsigned int)BK7258_AP_XIP_VECTOR_BASE);

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

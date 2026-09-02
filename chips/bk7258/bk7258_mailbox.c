/****************************************************************************
 * chips/bk7258/bk7258_mailbox.c
 *
 * Minimal Mailbox v2 doorbell transport. Payload ownership and framing stay
 * in RPMsg/VirtIO shared memory; FIFO entries carry only notification tokens.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_MAILBOX

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "nvic.h"
#include "include/bk7258_mailbox.h"
#include "include/bk7258_memorymap.h"

#define BK7258_MBOX_CTRL_OFFSET       UINT32_C(0x08)
#define BK7258_MBOX_INT_STATUS_OFFSET UINT32_C(0x0c)
#define BK7258_MBOX_CH_BASE(ch) \
  (BK7258_MBOX0_BASE + UINT32_C(0x40) + (uint32_t)(ch) * UINT32_C(0x40))
#define BK7258_MBOX_CH_CFG(ch)    (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x00))
#define BK7258_MBOX_CH_CTRL(ch)   (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x04))
#define BK7258_MBOX_CH_TDATA0(ch) (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x08))
#define BK7258_MBOX_CH_TDATA1(ch) (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x0c))
#define BK7258_MBOX_CH_TID(ch)    (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x10))
#define BK7258_MBOX_CH_SID(ch)    (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x14))
#define BK7258_MBOX_CH_RDATA0(ch) (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x18))
#define BK7258_MBOX_CH_RDATA1(ch) (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x1c))
#define BK7258_MBOX_CH_FSTAT(ch)  (BK7258_MBOX_CH_BASE(ch) + UINT32_C(0x20))

#define BK7258_MBOX_CTRL_RESET_RELEASE       (UINT32_C(1) << 0)
#define BK7258_MBOX_CTRL_CHANNEL_UNPROTECTED (UINT32_C(1) << 2)
#define BK7258_MBOX_CH_INT_ENABLE            (UINT32_C(1) << 8)
#define BK7258_MBOX_CH_ENABLE                (UINT32_C(1) << 0)
#define BK7258_MBOX_CH_LENGTH(len)            ((uint32_t)(len) << 1)
#define BK7258_MBOX_FIFO_FULL                 (UINT32_C(1) << 0)
#define BK7258_MBOX_FIFO_EMPTY                (UINT32_C(1) << 1)

#define BK7258_MBOX_CHANNELS 3

struct bk7258_mbox_fifo_s
{
  uint8_t start;
  uint8_t length;
};

static const struct bk7258_mbox_fifo_s g_bk7258_mbox_fifo[] =
{
  {0, 2},
  {2, 3},
  {5, 3},
};

static spinlock_t g_bk7258_mbox_lock = SP_UNLOCKED;
static bk7258_mbox_callback_t g_bk7258_mbox_callback;
static void *g_bk7258_mbox_callback_arg;
static bk7258_mbox_callback_t g_bk7258_mbox_ipi_callback;
static void *g_bk7258_mbox_ipi_callback_arg;
static bool g_bk7258_mbox_initialized;
static uint32_t g_bk7258_mbox_smp_pending;

/* The BK7258 has three physical cores: CP owns CPU0 and the AP component owns
 * CPU1 (logical0) plus CPU2 (logical1).  Each core drains the FIFO channel
 * that carries messages addressed to it, so the local channel index must
 * follow the running physical core, not a compile-time constant.
 *
 * Channel wiring:
 *
 *   channel 0: CPU0 (CP)            - RPTUN peer of channel 1
 *   channel 1: CPU1 (AP logical0)   - RPTUN peer of channel 0, and SMP IPI
 *                                     source/target for CPU2
 *   channel 2: CPU2 (AP logical1)   - SMP IPI source/target for CPU1 only
 *
 * A sender always transmits from its OWN core channel and sets TID to the
 * target core channel.  The receiver drains its own channel.  This is why the
 * SMP IPI send channel follows the running core (CPU1 -> ch1, CPU2 -> ch2),
 * while the RPTUN doorbell to CP must ALWAYS use channel 1 regardless of which
 * AP core the RPTUN worker happens to run on.
 */

static inline unsigned int bk7258_mbox_local_cpu(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  return 0;
#else
  return (unsigned int)(up_cpu_index() + 1);
#endif
}

/* The RPTUN (CP <-> AP) doorbell is physically bound to channel 1 on the AP
 * side and channel 0 on the CP side.  It must not follow the running core:
 * channel 2 only connects CPU1 and CPU2 inside the AP SMP domain.
 */

static inline unsigned int bk7258_mbox_rptun_local_channel(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  return 0;
#else
  return 1;
#endif
}

static void bk7258_mbox_route_irq(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  modifyreg32(BK7258_SYS_CPU0_INT_EN_HI, 0, BK7258_SYS_MAILBOX_INT_EN);
#else
  modifyreg32(BK7258_SYS_CPU1_INT_EN_HI, 0, BK7258_SYS_MAILBOX_INT_EN);
#  ifdef CONFIG_SMP
  modifyreg32(BK7258_SYS_CPU2_INT_EN_HI, 0, BK7258_SYS_MAILBOX_INT_EN);
#  endif
#endif
}

static void bk7258_mbox_drain(bool dispatch)
{
  unsigned int channel = bk7258_mbox_local_cpu();

  while ((getreg32(BK7258_MBOX_CH_FSTAT(channel)) &
          BK7258_MBOX_FIFO_EMPTY) == 0)
    {
      uint8_t src = getreg32(BK7258_MBOX_CH_SID(channel)) & UINT32_C(0xf);
      uint32_t data0 = getreg32(BK7258_MBOX_CH_RDATA0(channel));
      uint32_t data1 = getreg32(BK7258_MBOX_CH_RDATA1(channel));

      if (!dispatch)
        {
          continue;
        }

      if (data1 == BK7258_MBOX_SMP_MAGIC &&
          g_bk7258_mbox_ipi_callback != NULL)
        {
          /* AP SMP IPI. The RPTUN doorbell and the AP-internal SMP IPI share
           * one Mailbox IRQ, so the ISR demuxes by message type. */

          g_bk7258_mbox_smp_pending &= ~(UINT32_C(1) << channel);
          g_bk7258_mbox_ipi_callback(src, data0, data1,
                                     g_bk7258_mbox_ipi_callback_arg);
        }
      else if (g_bk7258_mbox_callback != NULL)
        {
          g_bk7258_mbox_callback(src, data0, data1,
                                 g_bk7258_mbox_callback_arg);
        }
    }
}

static int bk7258_mbox_interrupt(int irq, void *context, void *arg)
{
  uint32_t status = getreg32(BK7258_MBOX0_BASE +
                             BK7258_MBOX_INT_STATUS_OFFSET);

  (void)irq;
  (void)context;
  (void)arg;

  if ((status & (UINT32_C(1) << bk7258_mbox_local_cpu())) != 0)
    {
      bk7258_mbox_drain(true);
    }

  return 0;
}

int bk7258_mbox_attach(bk7258_mbox_callback_t callback, void *arg)
{
  irqstate_t flags = spin_lock_irqsave(&g_bk7258_mbox_lock);

  g_bk7258_mbox_callback = callback;
  g_bk7258_mbox_callback_arg = arg;
  spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
  return 0;
}

void bk7258_mbox_discard(unsigned int channel)
{
  /* Warm reset clears SRAM but not the Mailbox peripheral: FIFO entries of a
   * previous life survive.  A stale SMP_MAGIC entry read by the ISR would run
   * the SMP-call path on a core whose scheduler does not exist yet, so every
   * channel must be emptied without dispatch before its interrupt is armed. */

  while ((getreg32(BK7258_MBOX_CH_FSTAT(channel)) &
          BK7258_MBOX_FIFO_EMPTY) == 0)
    {
      getreg32(BK7258_MBOX_CH_SID(channel));
      getreg32(BK7258_MBOX_CH_RDATA0(channel));
      getreg32(BK7258_MBOX_CH_RDATA1(channel));
    }
}

void bk7258_mbox_discard_local(void)
{
  /* Discard the running core's stale FIFO entries and clear the NVIC pending
   * bit: the SYS-level route of a previous life may have kept the mailbox
   * line asserted, and a pending interrupt that fires on the next
   * up_enable_irq() must not find anything to dispatch. */

  unsigned int external = BK7258_IRQ_MAILBOX - NVIC_IRQ_FIRST;

  bk7258_mbox_discard(bk7258_mbox_local_cpu());
  putreg32(UINT32_C(1) << (external & 31), NVIC_IRQ_CLRPEND(external));
}

int bk7258_mbox_attach_ipi(bk7258_mbox_callback_t callback, void *arg)
{
  irqstate_t flags = spin_lock_irqsave(&g_bk7258_mbox_lock);

  g_bk7258_mbox_ipi_callback = callback;
  g_bk7258_mbox_ipi_callback_arg = arg;
  spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
  return 0;
}

int bk7258_mbox_init(bool global_owner)
{
  unsigned int channel = bk7258_mbox_local_cpu();
  int ret;

  if (g_bk7258_mbox_initialized)
    {
      return 0;
    }

  if (global_owner)
    {
      putreg32(BK7258_MBOX_CTRL_RESET_RELEASE |
               BK7258_MBOX_CTRL_CHANNEL_UNPROTECTED,
               BK7258_MBOX0_BASE + BK7258_MBOX_CTRL_OFFSET);

      for (channel = 0; channel < BK7258_MBOX_CHANNELS; channel++)
        {
          putreg32(g_bk7258_mbox_fifo[channel].start,
                   BK7258_MBOX_CH_CFG(channel));
          putreg32(BK7258_MBOX_CH_ENABLE |
                   BK7258_MBOX_CH_LENGTH(g_bk7258_mbox_fifo[channel].length),
                   BK7258_MBOX_CH_CTRL(channel));
        }

      channel = bk7258_mbox_local_cpu();
    }

  /* The AP component owns both physical CPU1 and CPU2.  Enable the FIFO
   * channel IRQ for every core the local instance can receive on, so the
   * AP SMP IPI path does not depend on when each core first boots. */

  modifyreg32(BK7258_MBOX_CH_CFG(channel), 0,
              BK7258_MBOX_CH_INT_ENABLE);
#ifdef CONFIG_BK7258_COMPONENT_AP
#  ifdef CONFIG_SMP
  /* Empty the sibling AP channel (physical CPU1<->CPU2) BEFORE arming its
   * FIFO interrupt: a stale entry from a previous boot would otherwise
   * assert the mailbox line to that core and run the SMP-call path there
   * before its scheduler exists. */

  bk7258_mbox_discard(bk7258_mbox_local_cpu() == 1 ? 2 : 1);
  modifyreg32(BK7258_MBOX_CH_CFG(bk7258_mbox_local_cpu() == 1 ? 2 : 1),
              0, BK7258_MBOX_CH_INT_ENABLE);
#  endif
#endif

  /* Discard stale entries before the IRQ is attached.  They belong to an
   * earlier boot and must not be reported as live Mailbox delivery.
   */

  bk7258_mbox_drain(false);

  ret = irq_attach(BK7258_IRQ_MAILBOX, bk7258_mbox_interrupt, NULL);
  if (ret < 0)
    {
      return ret;
    }

  up_prioritize_irq(BK7258_IRQ_MAILBOX,
                    CONFIG_BK7258_MAILBOX_IRQ_PRIORITY);
  bk7258_mbox_route_irq();
  up_enable_irq(BK7258_IRQ_MAILBOX);
  g_bk7258_mbox_initialized = true;
  return 0;
}

static int bk7258_mbox_send_locked(unsigned int channel, int dst_cpu,
                                   uint32_t data0, uint32_t data1)
{
  if (!g_bk7258_mbox_initialized)
    {
      return -ENODEV;
    }

  if (dst_cpu < 0 || dst_cpu >= BK7258_MBOX_CHANNELS ||
      dst_cpu == channel)
    {
      return -EINVAL;
    }

  if ((getreg32(BK7258_MBOX_CH_FSTAT(dst_cpu)) &
       BK7258_MBOX_FIFO_FULL) != 0)
    {
      return -EBUSY;
    }

  __asm__ volatile ("dmb" : : : "memory");
  putreg32(data0, BK7258_MBOX_CH_TDATA0(channel));
  putreg32(data1, BK7258_MBOX_CH_TDATA1(channel));
  putreg32((uint32_t)dst_cpu, BK7258_MBOX_CH_TID(channel));
  return 0;
}

int bk7258_mbox_notify(int dst_cpu, uint32_t token)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_mbox_lock);
  ret = bk7258_mbox_send_locked(bk7258_mbox_rptun_local_channel(),
                                dst_cpu, token, BK7258_MBOX_RPTUN_MAGIC);
  spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
  return ret;
}

int bk7258_mbox_ipi(int dst_cpu)
{
  unsigned int attempts = 0;
  irqstate_t flags;
  int ret;

  if (dst_cpu == bk7258_mbox_local_cpu())
    {
      return -EINVAL;
    }

  /* Keep at most one outstanding SMP kick in each direction.  The receiver
   * clears the shared latch before scanning the SMP queues, so a coalesced
   * kick can never be lost.  A full FIFO is transient: the target core's ISR
   * drains its channel without taking the send lock, so retry briefly rather
   * than drop the wakeup. */

  flags = spin_lock_irqsave(&g_bk7258_mbox_lock);
  if ((g_bk7258_mbox_smp_pending & (UINT32_C(1) << dst_cpu)) != 0)
    {
      spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
      return 0;
    }

  g_bk7258_mbox_smp_pending |= UINT32_C(1) << dst_cpu;

  do
    {
      ret = bk7258_mbox_send_locked(bk7258_mbox_local_cpu(), dst_cpu, 0,
                                    BK7258_MBOX_SMP_MAGIC);
      attempts++;
    }
  while (ret == -EBUSY && attempts < 16);

  if (ret < 0)
    {
      g_bk7258_mbox_smp_pending &= ~(UINT32_C(1) << dst_cpu);
    }

  spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
  return ret;
}

#endif /* CONFIG_BK7258_MAILBOX */

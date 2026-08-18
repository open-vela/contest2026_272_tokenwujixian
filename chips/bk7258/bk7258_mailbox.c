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
static bool g_bk7258_mbox_initialized;

#ifdef CONFIG_BK7258_COMPONENT_CP
#  define BK7258_MBOX_LOCAL_CPU 0
#else
#  define BK7258_MBOX_LOCAL_CPU 1
#endif

static void bk7258_mbox_route_irq(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  modifyreg32(BK7258_SYS_CPU0_INT_EN_HI, 0, BK7258_SYS_MAILBOX_INT_EN);
#else
  modifyreg32(BK7258_SYS_CPU1_INT_EN_HI, 0, BK7258_SYS_MAILBOX_INT_EN);
#endif
}

static void bk7258_mbox_drain(bool dispatch)
{
  unsigned int channel = BK7258_MBOX_LOCAL_CPU;

  while ((getreg32(BK7258_MBOX_CH_FSTAT(channel)) &
          BK7258_MBOX_FIFO_EMPTY) == 0)
    {
      uint8_t src = getreg32(BK7258_MBOX_CH_SID(channel)) & UINT32_C(0xf);
      uint32_t data0 = getreg32(BK7258_MBOX_CH_RDATA0(channel));
      uint32_t data1 = getreg32(BK7258_MBOX_CH_RDATA1(channel));

      if (dispatch && g_bk7258_mbox_callback != NULL)
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

  if ((status & (UINT32_C(1) << BK7258_MBOX_LOCAL_CPU)) != 0)
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

int bk7258_mbox_init(bool global_owner)
{
  unsigned int channel = BK7258_MBOX_LOCAL_CPU;
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

      channel = BK7258_MBOX_LOCAL_CPU;
    }

  modifyreg32(BK7258_MBOX_CH_CFG(channel), 0,
              BK7258_MBOX_CH_INT_ENABLE);
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

int bk7258_mbox_notify(int dst_cpu, uint32_t token)
{
  unsigned int channel = BK7258_MBOX_LOCAL_CPU;
  irqstate_t flags;

  if (!g_bk7258_mbox_initialized)
    {
      return -ENODEV;
    }

  if (dst_cpu < 0 || dst_cpu >= BK7258_MBOX_CHANNELS ||
      dst_cpu == BK7258_MBOX_LOCAL_CPU)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bk7258_mbox_lock);
  if ((getreg32(BK7258_MBOX_CH_FSTAT(dst_cpu)) &
       BK7258_MBOX_FIFO_FULL) != 0)
    {
      spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
      return -EBUSY;
    }

  __asm__ volatile ("dmb" : : : "memory");
  putreg32(token, BK7258_MBOX_CH_TDATA0(channel));
  putreg32(BK7258_MBOX_RPTUN_MAGIC, BK7258_MBOX_CH_TDATA1(channel));
  putreg32((uint32_t)dst_cpu, BK7258_MBOX_CH_TID(channel));
  spin_unlock_irqrestore(&g_bk7258_mbox_lock, flags);
  return 0;
}

#endif /* CONFIG_BK7258_MAILBOX */

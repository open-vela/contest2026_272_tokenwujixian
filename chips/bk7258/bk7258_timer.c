/****************************************************************************
 * chips/bk7258/bk7258_timer.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/timers/timer.h>
#include <nuttx/timers/arch_timer.h>

#include <errno.h>
#include <nuttx/spinlock.h>
#include <syslog.h>
#include <stdbool.h>

#include "arm_internal.h"
#include "nvic.h"
#include "systick.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_irq.h"
#include "include/bk7258_timer.h"

static int g_bk7258_timer0_irq_count;
struct bk7258_timer0_lowerhalf_s
{
  struct timer_lowerhalf_s lower;
  spinlock_t lock;
  tccb_t callback;
  void *callback_arg;
  uint32_t timeout_us;
  uint32_t period;
  bool started;
};

static struct bk7258_timer0_lowerhalf_s g_bk7258_timer0;

/* Keep bring-up diagnostics bounded.  This is intentionally not the
 * permanent timer-driver observability mechanism; it only proves whether the
 * external IRQ reaches the NuttX ISR before the lower-half is implemented. */
static unsigned int g_bk7258_timer0_log_count;

static int bk7258_timer0_stop(struct timer_lowerhalf_s *lower);

static int bk7258_timer0_isr(int irq, void *context, void *arg)
{
  uint32_t status;

  (void)irq;
  (void)context;
  struct bk7258_timer0_lowerhalf_s *priv = &g_bk7258_timer0;
  uint32_t next_us;
  bool reload;

  (void)arg;
  status = getreg32(BK7258_TIMER0_CTRL);
  g_bk7258_timer0_irq_count++;
  if (g_bk7258_timer0_log_count < 8)
    {
      g_bk7258_timer0_log_count++;
      syslog(LOG_INFO,
             "[BK7258] timer0 ISR entered irq=%d count=%d ctrl=0x%08lx\n",
             irq, g_bk7258_timer0_irq_count, (unsigned long)status);
    }
  /* Match Armino timer_isr(): sample the channel status and clear it before
   * returning to NuttX. The controller uses the interrupt-enable/status field
   * as a write-to-clear handshake. */
  if ((status & BK7258_TIMER0_INT_ENABLE) != 0)
    {
      putreg32(status | BK7258_TIMER0_INT_ENABLE, BK7258_TIMER0_CTRL);
    }
  if (priv->callback == NULL)
    {
      return OK;
    }

  next_us = priv->timeout_us;
  reload = priv->callback(&next_us, priv->callback_arg);
  if (!reload)
    {
      priv->started = false;
      putreg32(0, BK7258_TIMER0_CTRL);
    }
  else if (next_us != 0 && next_us <= UINT32_MAX / 26)
    {
      priv->timeout_us = next_us;
      priv->period = next_us * 26;
      putreg32(priv->period, BK7258_TIMER0_COUNT0);
    }
  return OK;
}

static int bk7258_timer0_start(struct timer_lowerhalf_s *lower)
{
  struct bk7258_timer0_lowerhalf_s *priv =
    (struct bk7258_timer0_lowerhalf_s *)lower;
  uint32_t period = priv->period;
  irqstate_t flags;
  int ret;

  if (period == 0 || priv->callback == NULL)
    {
      return -EINVAL;
    }

  g_bk7258_timer0_irq_count = 0;
  g_bk7258_timer0_log_count = 0;
  ret = bk7258_icu_attach(3, bk7258_timer0_isr, NULL);
  if (ret < 0)
    {
      return ret;
    }

  flags = enter_critical_section();
  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN,
              BK7258_SYS_TIMER0_CLK_EN);
  /* Match Armino timer_ll_init(): select the 26 MHz XTAL and assert the
   * timer-group soft reset before programming the channel. */
  modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_TIMER0_XTAL,
              BK7258_SYS_TIMER0_XTAL);
  /* timer_ll_init(): disable channel and interrupt before the group reset. */
  putreg32(0, BK7258_TIMER0_CTRL);
  putreg32(UINT32_C(1), BK7258_TIMER0_GLOBAL_CTRL);
  /* timer_chan_enable_interrupt_common(): enable the ICU source and channel
   * interrupt before timer_hal_init_timer(), matching Armino's order. */
  putreg32(BK7258_TIMER0_INT_ENABLE, BK7258_TIMER0_CTRL);
  putreg32(period, BK7258_TIMER0_COUNT0);
  /* timer_hal_init_timer(): divider 0 and clear stale channel status. */
  modifyreg32(BK7258_TIMER0_CTRL, UINT32_C(0x78), 0);
  putreg32(BK7258_TIMER0_INT_ENABLE, BK7258_TIMER0_CTRL);
  for (unsigned int count = 0;
       count < 1000 &&
       (getreg32(BK7258_TIMER0_CTRL) & BK7258_TIMER0_INT_ENABLE) != 0;
       count++)
    {
    }
  if ((getreg32(BK7258_TIMER0_CTRL) & BK7258_TIMER0_INT_ENABLE) != 0)
    {
      leave_critical_section(flags);
      bk7258_icu_disable(3);
      return -ETIMEDOUT;
    }
  /* timer_hal_start_common(): start the channel only after status clear. */
  putreg32(BK7258_TIMER0_ENABLE, BK7258_TIMER0_CTRL);
  priv->started = true;
  leave_critical_section(flags);

  ret = bk7258_icu_enable(3);
  syslog(LOG_INFO,
         "[BK7258] timer0 started period=%lu ctrl=0x%08lx icu=0x%08lx ret=%d\n",
         (unsigned long)period,
         (unsigned long)getreg32(BK7258_TIMER0_CTRL),
         (unsigned long)getreg32(BK7258_SYS_CPU0_INT_EN), ret);
  if (ret < 0)
    {
      bk7258_timer0_stop(&priv->lower);
    }
  return ret;
}

static int bk7258_timer0_stop(struct timer_lowerhalf_s *lower)
{
  struct bk7258_timer0_lowerhalf_s *priv =
    (struct bk7258_timer0_lowerhalf_s *)lower;
  irqstate_t flags;
  uint32_t before;
  uint32_t after;
  unsigned int count;

  /* Stop delivery first so a final channel event cannot race the teardown. */
  bk7258_icu_disable(3);
  flags = enter_critical_section();
  before = getreg32(BK7258_TIMER0_CTRL);
  /* Disable the channel, then acknowledge the controller's W1C interrupt
   * status handshake.  A plain zero write stops the counter but leaves the
   * status bit latched on BK7258. */
  putreg32(0, BK7258_TIMER0_CTRL);
  putreg32(BK7258_TIMER0_INT_ENABLE, BK7258_TIMER0_CTRL);
  for (count = 0; count < 1000; count++)
    {
      if ((getreg32(BK7258_TIMER0_CTRL) & BK7258_TIMER0_INT_ENABLE) == 0)
        {
          break;
        }
    }
  after = getreg32(BK7258_TIMER0_CTRL);
  leave_critical_section(flags);
  irq_detach(BK7258_IRQ_TIMER0);
  syslog(LOG_INFO,
         "[BK7258] timer0 stopped count=%d ctrl-before=0x%08lx ctrl-after=0x%08lx clear-loops=%u\n",
         g_bk7258_timer0_irq_count,
         (unsigned long)before, (unsigned long)after, count);
  priv->started = false;
  return OK;
}

static void bk7258_timer0_setcallback(struct timer_lowerhalf_s *lower,
                                      tccb_t callback, void *arg)
{
  struct bk7258_timer0_lowerhalf_s *priv =
    (struct bk7258_timer0_lowerhalf_s *)lower;
  irqstate_t flags = spin_lock_irqsave(&priv->lock);
  priv->callback = callback;
  priv->callback_arg = arg;
  spin_unlock_irqrestore(&priv->lock, flags);
}

static int bk7258_timer0_getstatus(struct timer_lowerhalf_s *lower,
                                   struct timer_status_s *status)
{
  struct bk7258_timer0_lowerhalf_s *priv =
    (struct bk7258_timer0_lowerhalf_s *)lower;
  irqstate_t flags = spin_lock_irqsave(&priv->lock);
  status->flags = priv->started ? TCFLAGS_ACTIVE : 0;
  status->flags |= priv->callback != NULL ? TCFLAGS_HANDLER : 0;
  status->timeout = priv->timeout_us;
  status->timeleft = priv->started ? priv->timeout_us : 0;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int bk7258_timer0_settimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t timeout)
{
  struct bk7258_timer0_lowerhalf_s *priv =
    (struct bk7258_timer0_lowerhalf_s *)lower;
  if (timeout == 0 || timeout > UINT32_MAX / 26)
    {
      return -EINVAL;
    }
  priv->timeout_us = timeout;
  priv->period = timeout * 26;
  return OK;
}

static int bk7258_timer0_maxtimeout(struct timer_lowerhalf_s *lower,
                                    uint32_t *maxtimeout)
{
  (void)lower;
  *maxtimeout = UINT32_MAX / 26;
  return OK;
}

static const struct timer_ops_s g_bk7258_timer0_ops =
{
  .start = bk7258_timer0_start,
  .stop = bk7258_timer0_stop,
  .getstatus = bk7258_timer0_getstatus,
  .settimeout = bk7258_timer0_settimeout,
  .setcallback = bk7258_timer0_setcallback,
  .maxtimeout = bk7258_timer0_maxtimeout,
};

int bk7258_timer_initialize(void)
{
  g_bk7258_timer0.lower.ops = &g_bk7258_timer0_ops;
  spin_lock_init(&g_bk7258_timer0.lock);
  g_bk7258_timer0.timeout_us = 10000;
  g_bk7258_timer0.period = 260000;
  return timer_register("/dev/timer0", &g_bk7258_timer0.lower) == NULL ?
         -EIO : OK;
}

void up_timer_initialize(void)
{
  uint32_t regval;
  uint32_t reload;

  /* CP and AP use separate 32 kHz SysTick selectors for physical CPU0 and
   * physical CPU1. Do not touch the other domain's selector. */

  /* The Bootloader handoff does not define the SysTick state. Stop any
   * inherited counter and clear its pending exception before selecting the
   * source and letting the generic lower-half enable it. */

  putreg32(0, NVIC_SYSTICK_CTRL);
  putreg32(NVIC_INTCTRL_PENDSTCLR, NVIC_INTCTRL);

  regval = getreg32(BK7258_SYS_POWER_WAKEUP);
#ifdef CONFIG_BK7258_COMPONENT_AP
  regval |= BK7258_SYS_CPU1_SYSTICK_32K;
#else
  regval |= BK7258_SYS_CPU0_SYSTICK_32K;
#endif
  putreg32(regval, BK7258_SYS_POWER_WAKEUP);

  /* Seed the first interval before enabling SysTick so startup does not
   * depend on reset or Bootloader-retained reload/current values. */

  reload = CONFIG_BK7258_SYSTICK_CLOCK_HZ / CLK_TCK - 1;
  putreg32(reload, NVIC_SYSTICK_RELOAD);
  putreg32(0, NVIC_SYSTICK_CURRENT);

  /* SysTick must not retain the reset/Bootloader priority of zero. The
   * scheduler uses BASEPRI=0x80 for critical sections, so keep the first
   * timer interrupt at the normal NuttX system-handler priority. */

  up_prioritize_irq(NVIC_IRQ_SYSTICK, NVIC_SYSH_PRIORITY_DEFAULT);

  up_timer_set_lowerhalf(
    systick_initialize(false, CONFIG_BK7258_SYSTICK_CLOCK_HZ, -1));

}

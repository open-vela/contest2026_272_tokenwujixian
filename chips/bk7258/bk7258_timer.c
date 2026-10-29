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
#include "bk7258_internal.h"
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
  bool coreclk;
  uint32_t freq;

  /* Polling-only early-boot marker: SysTick is not enabled yet. */
  BK7258_BOOT_MARK('E');

  /* The Bootloader handoff does not define the SysTick state. Stop any
   * inherited counter and clear its pending exception before selecting the
   * source and letting the generic lower-half enable it. */

  putreg32(0, NVIC_SYSTICK_CTRL);
  putreg32(NVIC_INTCTRL_PENDSTCLR, NVIC_INTCTRL);

#ifdef CONFIG_BK7258_COMPONENT_AP
  /* AP cores run the system tick from the processor clock.  The board
   * bring-up on 2026-10-12 showed the effective tick supply at roughly
   * one tenth of the configured rate when CPU1 used its external 32 kHz
   * selector, so the AP no longer touches BK7258_SYS_POWER_WAKEUP at all:
   * that also removes an AP-side RMW on a CP-owned shared register. */

  freq = CONFIG_BK7258_AP_CPU_CLOCK_HZ;
  coreclk = true;
#else
  /* CP and AP use separate 32 kHz SysTick selectors for physical CPU0 and
   * physical CPU1. Do not touch the other domain's selector. */

  regval = getreg32(BK7258_SYS_POWER_WAKEUP);
  regval |= BK7258_SYS_CPU0_SYSTICK_32K;
  putreg32(regval, BK7258_SYS_POWER_WAKEUP);

  freq = CONFIG_BK7258_SYSTICK_CLOCK_HZ;
  coreclk = false;
#endif

  /* Seed the first interval before enabling SysTick so startup does not
   * depend on reset or Bootloader-retained reload/current values. */

  reload = freq / CLK_TCK - 1;
  putreg32(reload, NVIC_SYSTICK_RELOAD);
  putreg32(0, NVIC_SYSTICK_CURRENT);

  /* SysTick must not retain the reset/Bootloader priority of zero. The
   * scheduler uses BASEPRI=0x80 for critical sections, so keep the first
   * timer interrupt at the normal NuttX system-handler priority. */

  up_prioritize_irq(NVIC_IRQ_SYSTICK, NVIC_SYSH_PRIORITY_DEFAULT);

  up_timer_set_lowerhalf(systick_initialize(coreclk, freq, -1));

  BK7258_BOOT_MARK('F');
}

/****************************************************************************
 * Armino timer API face
 *
 * The Armino modules (wdt_driver on TIMER_ID2, touch_driver on TIMER_ID1,
 * rosc_ppm on TIMER_ID0, bk_system/delay.c, cli_timer) call the bk_timer_*
 * names from <driver/timer.h>.  Those resolved to
 * middleware/driver/timer/timer_driver.c, which sat on timer_hal.c /
 * timer_ll.h -- a second owner of the same block this file already drives
 * (SOC_TIMER0_REG_BASE == BK7258_TIMER0_BASE == 0x44810000).  rosc_ppm's
 * TIMER_ID0 is the very channel /dev/timer0 uses, so the two wrote the
 * same enable and end-count registers.
 *
 * Hardware layout (timer_cap.h + timer_struct.h): one unit, two groups of
 * three channels, so TIMER_ID0..5.  group0 is at 0x44810000 (channels 0-2)
 * and group1 at 0x45800000 (channels 3-5).  Per group:
 *   +0x08 global_ctrl   +0x10/+0x14/+0x18 timer_cnt[]   +0x1c ctrl
 *   +0x20 read_ctrl     +0x24 read_value
 * ctrl: [0:2] per-channel enable, [3:6] clk_div, [7:9] per-channel int_en.
 *
 * Channel ownership is not arbitrated here -- it follows the callers above,
 * same as the original.  /dev/timer0 keeps channel 0 (see
 * bk7258_timer_initialize) and rosc_ppm may reprogram it; that is the
 * pre-existing behaviour, not something this face introduces.
 ****************************************************************************/

#ifndef CONFIG_BK7258_COMPONENT_AP

#include <driver/timer.h>
#include <driver/int.h>

/* sys_driver.h drags the whole SoC HAL include chain (system_hw.h ->
 * sys_reg.h -> sys_ll.h) and each level shadows another on this path, so
 * the three sys_drv_* calls the timer face needs are done on the registers
 * directly.  Bit positions from armino_baseline/cp/middleware/soc/bk7258/:
 *   sys_types.h:28/:38  TIMER_INTERRUPT_CTRL_BIT  = 1 << TIMER0_INT_EN_POS
 *   sys_reg.h:894/:924  TIMER0_INT_EN_POS = 3, TIMER1_INT_EN_POS = 13
 *   sys_reg.h:883       SYS_CPU0_INT_0_31_EN_ADDR = SOC_SYS_REG_BASE + 0x80
 *   sys_types.h:205/:214  CLK_PWR_ID_TIMER_1 = 4, CLK_PWR_ID_TIMER_2 = 13,
 *                       both indexes into SYS REG 0xc = BK7258_SYS_DEV_CLK_EN
 */

#define BK7258_SYS_CPU0_INT_EN   (BK7258_SYS_BASE + UINT32_C(0x80))
#define BK_TIMER0_ICU_BIT        (UINT32_C(1) << 3)
#define BK_TIMER1_ICU_BIT        (UINT32_C(1) << 13)
#define BK7258_SYS_TIMER1_CLK_EN (UINT32_C(1) << 13)
#include <soc/bk7258/timer_cap.h>

#define BK_TIMER_GROUP_NUM        SOC_TIMER_GROUP_NUM
#define BK_TIMER_CHAN_PER_GROUP   SOC_TIMER_CHAN_NUM_PER_GROUP
#define BK_TIMER_CHAN_NUM         SOC_TIMER_CHAN_NUM_PER_UNIT

#define BK_TIMER_GROUP_BASE(g) \
  ((g) == 0 ? BK7258_TIMER0_BASE : UINT32_C(0x45800000))

#define BK_TIMER_GLOBAL_CTRL(g)  (BK_TIMER_GROUP_BASE(g) + UINT32_C(0x08))
#define BK_TIMER_CNT(g, c)       (BK_TIMER_GROUP_BASE(g) + UINT32_C(0x10) + \
                                  ((uint32_t)(c) << 2))
#define BK_TIMER_CTRL(g)         (BK_TIMER_GROUP_BASE(g) + UINT32_C(0x1c))
#define BK_TIMER_READ_CTRL(g)    (BK_TIMER_GROUP_BASE(g) + UINT32_C(0x20))
#define BK_TIMER_READ_VALUE(g)   (BK_TIMER_GROUP_BASE(g) + UINT32_C(0x24))

/* timer_ll.h:209 -- clk_div is a 4-bit field at ctrl[6:3]. */

#define BK_TIMER_CLK_DIV_SHIFT   3
#define BK_TIMER_CLK_DIV_MASK    (UINT32_C(0xf) << BK_TIMER_CLK_DIV_SHIFT)

#define BK_TIMER_FREQ_26M_KHZ    TIMER_CLOCK_FREQ_26M
#define BK_TIMER_FREQ_32K_KHZ    TIMER_CLOCK_FREQ_32K

static void bk_timer_chan_irq_clear(uint32_t chan);

static timer_isr_t s_timer_isr[BK_TIMER_CHAN_NUM];
static bool s_timer_is_init;

#define TIMER_RETURN_ON_NOT_INIT() \
  do { if (!s_timer_is_init) return BK_ERR_TIMER_NOT_INIT; } while (0)
#define TIMER_RETURN_ON_INVALID_ID(id) \
  do { if ((id) >= BK_TIMER_CHAN_NUM) return BK_ERR_TIMER_ID; } while (0)

/* timer_driver.c:73 TIMER_RETURN_TIMER_ID_IS_ERR: CONFIG_TIMER_SUPPORT_ID_BITS
 * is 7 on this build, so only TIMER_ID0..2 are allowed and 3..5 answer
 * BK_ERR_TIMER_ID_ON_DEFCONFIG.  The earlier version accepted all six.
 */

#define TIMER_RETURN_ON_UNSUPPORTED_ID(id) \
  do { if ((~CONFIG_TIMER_SUPPORT_ID_BITS) & BIT(id)) \
         return BK_ERR_TIMER_ID_ON_DEFCONFIG; } while (0)

static inline uint32_t bk_timer_group(uint32_t chan)
{
  return chan / BK_TIMER_CHAN_PER_GROUP;
}

static inline uint32_t bk_timer_chan_in_group(uint32_t chan)
{
  return chan % BK_TIMER_CHAN_PER_GROUP;
}

/* The block is clocked from XTAL on this board (BK7258_SYS_TIMER0_XTAL),
 * so end counts use the 26 MHz figure.  timer_hal.c:56 picks the rate from
 * sys_hal_timer_select_clock_get(); with the clock fixed that collapses to
 * this branch.
 */

static uint32_t bk_timer_end_count(uint64_t time, timer_value_unit_t unit)
{
  uint64_t value;

  value = time * BK_TIMER_FREQ_26M_KHZ / (unit == TIMER_UNIT_MS ? 1 : 1000);
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

/* timer_ll_set_chan_interrupt_status writes ctrl.timerN_int_en, the same
 * bit timer_ll_set_interrupt uses to arm the interrupt -- on this block it
 * doubles as the pending flag, write-1-to-clear.  The clear loop mirrors
 * timer_ll_clear_chan_interrupt_status.
 */

static void bk_timer_chan_irq_clear(uint32_t chan)
{
  uint32_t g = bk_timer_group(chan);
  uint32_t c = bk_timer_chan_in_group(chan);
  uint32_t bit = UINT32_C(1) << (7 + c);

  putreg32(getreg32(BK_TIMER_CTRL(g)) | bit, BK_TIMER_CTRL(g));
  while (getreg32(BK_TIMER_CTRL(g)) & bit)
    {
    }
}

static void bk_timer_group_isr(uint32_t g)
{
  uint32_t regval = getreg32(BK_TIMER_CTRL(g));
  uint32_t c;

  for (c = 0; c < BK_TIMER_CHAN_PER_GROUP; c++)
    {
      uint32_t chan = g * BK_TIMER_CHAN_PER_GROUP + c;

      if ((regval & (UINT32_C(1) << (7 + c))) == 0)
        {
          continue;
        }

      bk_timer_chan_irq_clear(chan);
      if (s_timer_isr[chan] != NULL)
        {
          s_timer_isr[chan](chan);
        }
    }
}

static void bk_timer_group0_isr(void *arg)
{
  (void)arg;
  bk_timer_group_isr(0);
}

static void bk_timer_group1_isr(void *arg)
{
  (void)arg;
  bk_timer_group_isr(1);
}

bk_err_t bk_timer_driver_init(void)
{
  /* timer_driver.c:308/:310 -- one handler per interrupt group.  Without
   * these the timer fires and irq_unexpected_isr panics the board about
   * eight seconds after boot.
   */

  bk_int_isr_register(INT_SRC_TIMER, bk_timer_group0_isr, NULL);
#if (SOC_TIMER_INTERRUPT_NUM > 1)
  bk_int_isr_register(INT_SRC_TIMER1, bk_timer_group1_isr, NULL);
#endif

  s_timer_is_init = true;
  return BK_OK;
}

bk_err_t bk_timer_driver_deinit(void)
{
  uint32_t chan;

  if (!s_timer_is_init)
    {
      return BK_OK;
    }

  /* timer_chan_deinit_common for every channel: stop, reset the config to
   * default, and gate the group clock back off.  Just clearing the init
   * flag left the block running.
   */

  for (chan = 0; chan < BK_TIMER_CHAN_NUM; chan++)
    {
      bk_timer_stop(chan);
      bk_timer_cancel(chan);
    }

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN, 0);
  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER1_CLK_EN, 0);

  s_timer_is_init = false;
  return BK_OK;
}

bk_err_t bk_timer_start_without_callback(timer_id_t timer_id, uint32_t time_ms)
{
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);
  uint32_t g;
  uint32_t c;
  uint32_t regval;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);

  /* timer_driver.c:365: if the channel is running, stop it and wait 4
   * ticks before reprogramming -- a counter already past the new end count
   * would never reach it again.
   */

  regval = getreg32(BK_TIMER_CTRL(g));
  if (regval & (UINT32_C(1) << c))
    {
      putreg32(regval & ~(UINT32_C(1) << c), BK_TIMER_CTRL(g));
      up_udelay(4);
      regval = getreg32(BK_TIMER_CTRL(g));
    }

  putreg32(bk_timer_end_count(time_ms, TIMER_UNIT_MS), BK_TIMER_CNT(g, c));
  regval &= ~BK_TIMER_CLK_DIV_MASK;
  putreg32(regval, BK_TIMER_CTRL(g));

  /* timer_chan_init_common: pick XTAL and gate the group clock on.  Both are
   * per group, so TIMER_ID3..5 need this too -- only doing group 0 left
   * group 1's clock off.
   */

  if (g == 0)
    {
      modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_TIMER0_XTAL,
                  BK7258_SYS_TIMER0_XTAL);
      modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN,
                  BK7258_SYS_TIMER0_CLK_EN);
    }
  else
    {
      modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER1_CLK_EN,
                  BK7258_SYS_TIMER1_CLK_EN);
    }

  /* timer_chan_enable_interrupt_common: the ICU-level route first, then the
   * peripheral bit.  Missing the ICU step is what produced
   * irq_unexpected_isr -- the block fired onto a line nothing had attached.
   */

  modifyreg32(BK7258_SYS_CPU0_INT_EN, 0,
              g == 0 ? BK_TIMER0_ICU_BIT : BK_TIMER1_ICU_BIT);
  putreg32(getreg32(BK_TIMER_CTRL(g)) | (UINT32_C(1) << (7 + c)),
           BK_TIMER_CTRL(g));

  /* timer_ll_enable */

  putreg32(getreg32(BK_TIMER_CTRL(g)) | (UINT32_C(1) << c), BK_TIMER_CTRL(g));
  return BK_OK;
}

bk_err_t bk_timer_start(timer_id_t timer_id, uint32_t time_ms,
                        timer_isr_t callback)
{
  bk_err_t ret;

  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);

  TIMER_RETURN_ON_INVALID_ID(timer_id);
  ret = bk_timer_start_without_callback(timer_id, time_ms);
  if (ret == BK_OK)
    {
      s_timer_isr[timer_id] = callback;
    }

  return ret;
}

bk_err_t bk_timer_stop(timer_id_t timer_id)
{
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);
  uint32_t g;
  uint32_t c;
  uint32_t regval;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);

  /* timer_hal_stop_common: int off, pending clear, channel off.  The
   * pending bit is write-1-to-clear and must be drained, or the next enable
   * re-fires immediately.
   */

  regval = getreg32(BK_TIMER_CTRL(g));
  regval &= ~((UINT32_C(1) << c) | (UINT32_C(1) << (7 + c)));
  putreg32(regval, BK_TIMER_CTRL(g));
  bk_timer_chan_irq_clear(timer_id);
  return BK_OK;
}

bk_err_t bk_timer_cancel(timer_id_t timer_id)
{
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);
  uint32_t g;
  uint32_t c;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);

  /* timer_ll_reset_config_to_default */

  putreg32(0, BK_TIMER_CNT(g, c));
  putreg32(getreg32(BK_TIMER_CTRL(g)) & ~BK_TIMER_CLK_DIV_MASK,
           BK_TIMER_CTRL(g));
  s_timer_isr[timer_id] = NULL;
  return BK_OK;
}

uint32_t bk_timer_get_cnt(timer_id_t timer_id)
{
  uint32_t g;
  uint32_t c;
  uint32_t regval;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);

  if ((getreg32(BK_TIMER_CTRL(g)) & (UINT32_C(1) << c)) == 0)
    {
      return 0;
    }

  /* timer_hal_get_count: ask the block to latch the channel counter into
   * read_value, then wait for the request bit to clear.
   */

  regval = getreg32(BK_TIMER_READ_CTRL(g));
  regval &= ~UINT32_C(0xc);
  regval |= (c << 2) | UINT32_C(1);
  putreg32(regval, BK_TIMER_READ_CTRL(g));

  while (getreg32(BK_TIMER_READ_CTRL(g)) & UINT32_C(1))
    {
    }

  return getreg32(BK_TIMER_READ_VALUE(g));
}

bk_err_t bk_timer_enable(timer_id_t timer_id)
{
  uint32_t g;
  uint32_t c;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);
  putreg32(getreg32(BK_TIMER_CTRL(g)) | (UINT32_C(1) << c),
           BK_TIMER_CTRL(g));
  return BK_OK;
}

bk_err_t bk_timer_disable(timer_id_t timer_id)
{
  uint32_t g;
  uint32_t c;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);
  putreg32(getreg32(BK_TIMER_CTRL(g)) & ~(UINT32_C(1) << c),
           BK_TIMER_CTRL(g));
  return BK_OK;
}

uint32_t bk_timer_get_period(timer_id_t timer_id)
{
  uint32_t g;
  uint32_t c;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);
  return getreg32(BK_TIMER_CNT(g, c));
}

uint32_t bk_timer_get_enable_status(void)
{
  uint32_t status = 0;
  uint32_t g;

  TIMER_RETURN_ON_NOT_INIT();

  for (g = 0; g < BK_TIMER_GROUP_NUM; g++)
    {
      uint32_t regval = getreg32(BK_TIMER_CTRL(g));
      status |= (regval & UINT32_C(0x7)) << (g * BK_TIMER_CHAN_PER_GROUP);
    }

  return status;
}

/* timer_ll_get_interrupt_status reads ctrl[9:7] -- the per-channel int_en
 * bits, NOT ctrl[2:0] the channel enables.  The earlier version looked at
 * the wrong field; the two are independent bits in the same word.
 */

static uint32_t bk_timer_int_status(void)
{
  uint32_t status = 0;
  uint32_t g;

  for (g = 0; g < BK_TIMER_GROUP_NUM; g++)
    {
      uint32_t regval = getreg32(BK_TIMER_CTRL(g));
      status |= ((regval >> 7) & UINT32_C(0x7)) << (g * BK_TIMER_CHAN_PER_GROUP);
    }

  return status;
}

bool bk_timer_is_interrupt_triggered(timer_id_t timer_id)
{
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  return (bk_timer_int_status() & BIT(timer_id)) != 0;
}

uint32_t timer_clear_isr_status(void)
{
  uint32_t status = bk_timer_int_status();
  uint32_t chan;

  for (chan = 0; chan < BK_TIMER_CHAN_NUM; chan++)
    {
      if (status & BIT(chan))
        {
          bk_timer_chan_irq_clear(chan);
        }
    }

  return status;
}

/* timer_hal.c:68 -- TIMER_ID0 doubles as a free-running 26 MHz counter for
 * the busy-wait delay.  It enables channel 0 if nothing has it running and
 * spins until 26*us ticks have gone by; this is what the
 * "timer0 is reserved for us timer" warning in bk_timer_start_without_
 * callback refers to.  The end count is left alone, so the channel keeps
 * wrapping and the unsigned difference below stays valid.
 */

/* timer_hal.c:43 -- arm TIMER_ID0 as a free-running 26 MHz counter:
 * soft-reset the group, load an all-ones end count, enable channel 0, and
 * gate its clock with XTAL as the source.  Called from sys_hal.c:2739
 * during the clock reprogram, before anything takes a delay.  The header
 * declares it as timer_hal_us_init(void) while the body takes a us
 * argument that it ignores; the no-argument form is what compiles.
 */

void timer_hal_us_init(void)
{
  uint32_t regval;

  /* global_ctrl.soft_reset */

  putreg32(UINT32_C(1), BK_TIMER_GLOBAL_CTRL(0));

  putreg32(UINT32_MAX, BK_TIMER_CNT(0, 0));
  putreg32(getreg32(BK_TIMER_CTRL(0)) | UINT32_C(1), BK_TIMER_CTRL(0));

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN,
              BK7258_SYS_TIMER0_CLK_EN);
  modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_TIMER0_XTAL,
              BK7258_SYS_TIMER0_XTAL);
}

/* Read the channel-0 counter without the bk_timer_get_cnt guards: this runs
 * from the clock reprogram (sys_hal.c) long before bk_timer_driver_init(),
 * and a call that returned 0 every time would spin forever on
 * while (end - begin < delay_cycle).  That is a boot loop, not a hang.
 */

static uint32_t bk_timer_cnt_raw(void)
{
  uint32_t regval;

  regval = getreg32(BK_TIMER_READ_CTRL(0));
  regval &= ~UINT32_C(0xc);
  regval |= UINT32_C(1);
  putreg32(regval, BK_TIMER_READ_CTRL(0));
  while (getreg32(BK_TIMER_READ_CTRL(0)) & UINT32_C(1))
    {
    }

  return getreg32(BK_TIMER_READ_VALUE(0));
}

/* timer_driver.c:544 -- convert a channel counter to a time in the caller's
 * unit.  The count is relative to last_count and scaled by div; the rate is
 * the 26 MHz XTAL this board runs the timer blocks from.
 */

uint64_t bk_timer_get_time(timer_id_t timer_id, uint32_t div,
                           uint32_t last_count, timer_value_unit_t unit_type)
{
  uint64_t current_count;
  uint64_t unit_factor;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  if (div == 0)
    {
      div = 1;
    }

  current_count = (uint64_t)bk_timer_get_cnt(timer_id) + last_count;
  unit_factor = (unit_type == TIMER_UNIT_MS) ? 1 : 1000;
  return unit_factor * current_count * div / BK_TIMER_FREQ_26M_KHZ;
}

/* timer_driver.c:599 -- arm the channel for a one-shot after time_us and
 * hand the tick to callback.  The end count is absolute (current + delta),
 * not a reload value, so the counter keeps running and wraps.
 */

bk_err_t bk_timer_delay_with_callback(timer_id_t timer_id, uint64_t time_us,
                                      timer_isr_t callback)
{
  uint64_t current_count;
  uint64_t delta_count;
  uint64_t end_count;
  uint32_t g;
  uint32_t c;
  uint32_t regval;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);

  g = bk_timer_group(timer_id);
  c = bk_timer_chan_in_group(timer_id);

  if (g == 0)
    {
      modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_TIMER0_XTAL,
                  BK7258_SYS_TIMER0_XTAL);
      modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN,
                  BK7258_SYS_TIMER0_CLK_EN);
    }
  else
    {
      modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER1_CLK_EN,
                  BK7258_SYS_TIMER1_CLK_EN);
    }

  modifyreg32(BK7258_SYS_CPU0_INT_EN, 0,
              g == 0 ? BK_TIMER0_ICU_BIT : BK_TIMER1_ICU_BIT);
  putreg32(getreg32(BK_TIMER_CTRL(g)) | (UINT32_C(1) << (7 + c)),
           BK_TIMER_CTRL(g));

  current_count = bk_timer_get_cnt(timer_id);
  delta_count = bk_timer_end_count(time_us, TIMER_UNIT_US);
  end_count = current_count + delta_count;
  if (end_count > UINT32_MAX)
    {
      end_count = UINT32_MAX;
    }

  putreg32((uint32_t)end_count, BK_TIMER_CNT(g, c));
  regval = getreg32(BK_TIMER_CTRL(g)) & ~BK_TIMER_CLK_DIV_MASK;
  putreg32(regval, BK_TIMER_CTRL(g));
  bk_timer_chan_irq_clear(timer_id);
  putreg32(regval | (UINT32_C(1) << c), BK_TIMER_CTRL(g));

  s_timer_isr[timer_id] = callback;
  return BK_OK;
}

void bk_timer_delay_us(uint32_t us)
{
  uint32_t delay_cycle = 26u * us;
  uint32_t begin;
  uint32_t end;

  if ((getreg32(BK_TIMER_CTRL(0)) & UINT32_C(1)) == 0)
    {
      putreg32(getreg32(BK_TIMER_CTRL(0)) | UINT32_C(1), BK_TIMER_CTRL(0));
    }

  begin = bk_timer_cnt_raw();
  do
    {
      end = bk_timer_cnt_raw();
    }
  while ((uint32_t)(end - begin) < delay_cycle);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

#ifdef CONFIG_SMP
/****************************************************************************
 * Name: bk7258_cpu2_timer_initialize
 *
 * Description:
 *   Start the per-core system tick on the AP SMP secondary core (physical
 *   CPU2, logical CPU1).
 *
 *   The AP system tick runs from the processor clock (CLKSOURCE = 1) on
 *   every AP core, matching the primary core's up_timer_initialize().
 *   The interrupt handler is the shared arm_systick.c systick_interrupt
 *   attached by the primary core: the irq_attach() table is common to
 *   every AP core and the handler only touches the running core's SysTick
 *   registers, so no separate lower-half instance is needed (and none can
 *   be added: up_timer_set_lowerhalf() is a system-wide singleton).  The
 *   reload must use the same CONFIG_BK7258_AP_CPU_CLOCK_HZ as the primary
 *   core: the shared lower half derives interval arithmetic from its
 *   freq, and a mismatched reload would corrupt every sub-tick time
 *   reading taken on this core.
 *
 *   Every tick calls nxsched_process_timer(), whose paths are SMP-safe:
 *   clock_timer() is seqlock-protected, wd_timer() runs inside a critical
 *   section on the global watchdog list, and the round-robin timeslice
 *   bookkeeping is per-CPU.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 * Assumptions:
 *   Runs on physical CPU2 after up_irqinitialize(); the per-CPU idle TCB
 *   and scheduler lists for this core already exist (idle_task_initialize()
 *   runs long before up_cpu_start()).
 *
 ****************************************************************************/

void bk7258_cpu2_timer_initialize(void)
{
  uint32_t reload;

  /* The Bootloader handoff does not define the SysTick state. Stop any
   * inherited counter and clear its pending exception before reprogramming
   * the timer. */

  putreg32(0, NVIC_SYSTICK_CTRL);
  putreg32(NVIC_INTCTRL_PENDSTCLR, NVIC_INTCTRL);

  /* Core-clock mode: reload = fcpu / CLK_TCK - 1, with the same frequency
   * (and therefore the same reload) as the primary core's tick, since the
   * shared lower half assumes one consistent interval on every core. */

  reload = CONFIG_BK7258_AP_CPU_CLOCK_HZ / CLK_TCK - 1;
  putreg32(reload, NVIC_SYSTICK_RELOAD);
  putreg32(0, NVIC_SYSTICK_CURRENT);

  /* SysTick must not retain the reset priority of zero; match the primary
   * core's up_timer_initialize() setup (BASEPRI critical sections use
   * 0x80). */

  up_prioritize_irq(NVIC_IRQ_SYSTICK, NVIC_SYSH_PRIORITY_DEFAULT);

  /* Start the counter: ENABLE | TICKINT | CLKSOURCE (= processor clock).
   * The exception dispatches through the shared vector table to the
   * systick_interrupt handler attached on the primary core. */

  putreg32(NVIC_SYSTICK_CTRL_ENABLE | NVIC_SYSTICK_CTRL_TICKINT |
           NVIC_SYSTICK_CTRL_CLKSOURCE,
           NVIC_SYSTICK_CTRL);
}
#endif /* CONFIG_SMP */

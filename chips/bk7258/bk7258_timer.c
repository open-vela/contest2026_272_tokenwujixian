/****************************************************************************
 * chips/bk7258/bk7258_timer.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/timers/timer.h>
#include <nuttx/timers/arch_timer.h>

#include <errno.h>
#include <stdbool.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "nvic.h"
#include "systick.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_irq.h"
#include "include/bk7258_timer.h"

void up_timer_initialize(void)
{
  uint32_t regval;
  uint32_t reload;

  /* Polling-only early-boot marker: SysTick is not enabled yet. */
  BK7258_BOOT_MARK('E');

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
 * (SOC_TIMER0_REG_BASE == BK7258_TIMER0_BASE == 0x44810000).
 *
 * Hardware layout (timer_cap.h + timer_struct.h): one unit, two groups of
 * three channels, so TIMER_ID0..5.  group0 is at 0x44810000 (channels 0-2)
 * and group1 at 0x45800000 (channels 3-5).  Per group:
 *   +0x08 global_ctrl   +0x10/+0x14/+0x18 timer_cnt[]   +0x1c ctrl
 *   +0x20 read_ctrl     +0x24 read_value
 * ctrl: [0:2] per-channel enable, [3:6] clk_div, [7:9] per-channel int_en.
 *
 * Channel ownership follows the callers above, same as the original:
 * TIMER_ID0 is the bk_timer_delay_us() counter, TIMER_ID2 the watchdog
 * feed.  The NuttX face at the end of this file exposes TIMER_ID1 only.
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

/* One state record per channel, owned by the NuttX lower half.  The
 * bk_timer_* face reads and writes the same records: isr is the baseline's
 * s_timer_isr[], timeout_us is the period the channel was last programmed
 * with (by either face), and callback/arg are the upper half's.
 */

struct bk7258_timer_s
{
  struct timer_lowerhalf_s lower;
  timer_id_t               id;
  timer_isr_t              isr;
  tccb_t                   callback;
  void                    *arg;
  uint32_t                 timeout_us;
};

static struct bk7258_timer_s g_bk7258_timer[BK_TIMER_CHAN_NUM];
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
      if (g_bk7258_timer[chan].isr != NULL)
        {
          g_bk7258_timer[chan].isr(chan);
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

/* The register sequence of bk_timer_start_without_callback(), shared with
 * the NuttX face so both program a channel the same way.  Callers have done
 * the id and init checks.
 */

static void bk_timer_program(timer_id_t timer_id, uint32_t end_count)
{
  uint32_t g;
  uint32_t c;
  uint32_t regval;

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

  putreg32(end_count, BK_TIMER_CNT(g, c));
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
}

bk_err_t bk_timer_start_without_callback(timer_id_t timer_id, uint32_t time_ms)
{
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  bk_timer_program(timer_id, bk_timer_end_count(time_ms, TIMER_UNIT_MS));
  g_bk7258_timer[timer_id].timeout_us =
    time_ms > UINT32_MAX / 1000 ? UINT32_MAX : time_ms * 1000;
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
      g_bk7258_timer[timer_id].isr = callback;
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
  g_bk7258_timer[timer_id].isr        = NULL;
  g_bk7258_timer[timer_id].timeout_us = 0;
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

  g_bk7258_timer[timer_id].isr = callback;
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

/****************************************************************************
 * NuttX timer face
 *
 * /dev/timer1 is TIMER_ID1 seen through NuttX's timer framework.  Its state
 * is the channel's record in g_bk7258_timer, the one the bk_timer_* face
 * keeps; it programs the channel with bk_timer_program() and reads it back
 * through bk_timer_get_*(), so there is one register path.  The interrupt
 * stays with bk_timer_group_isr(): the channel's isr slot points at
 * bk7258_timer_tick(), which runs the upper half's callback.
 *
 * Only TIMER_ID1 is exposed.  TIMER_ID0 is the free-running counter behind
 * bk_timer_delay_us() and TIMER_ID2 carries the 1 s watchdog feed; giving
 * either to user space would let an ioctl stop them.  TIMER_ID1's Armino
 * user was the touch driver, which is no longer in the image.
 ****************************************************************************/

#ifdef CONFIG_BK7258_TIMER

#define BK7258_TIMER_DEV_ID  TIMER_ID1
#define BK7258_TIMER_MAX_US  (UINT32_MAX / 26)  /* 26 MHz XTAL, 32-bit count */

static void bk7258_timer_tick(timer_id_t timer_id)
{
  struct bk7258_timer_s *priv = &g_bk7258_timer[timer_id];
  uint32_t next_us = priv->timeout_us;

  /* No handler: the channel just keeps reloading, like a bk_timer_start()
   * channel with a NULL callback.
   */

  if (priv->callback == NULL)
    {
      return;
    }

  if (!priv->callback(&next_us, priv->arg))
    {
      bk_timer_stop(timer_id);
      return;
    }

  if (next_us != priv->timeout_us && next_us != 0 &&
      next_us <= BK7258_TIMER_MAX_US)
    {
      priv->timeout_us = next_us;
      bk_timer_program(timer_id, bk_timer_end_count(next_us, TIMER_UNIT_US));
    }
}

static int bk7258_timer_start(struct timer_lowerhalf_s *lower)
{
  struct bk7258_timer_s *priv = (struct bk7258_timer_s *)lower;

  if (!s_timer_is_init)
    {
      return -ENODEV;
    }

  if (priv->timeout_us == 0)
    {
      return -EINVAL;
    }

  /* Point the isr slot at the tick before the channel can fire. */

  priv->isr = bk7258_timer_tick;
  bk_timer_program(priv->id,
                   bk_timer_end_count(priv->timeout_us, TIMER_UNIT_US));
  return OK;
}

static int bk7258_timer_stop(struct timer_lowerhalf_s *lower)
{
  struct bk7258_timer_s *priv = (struct bk7258_timer_s *)lower;

  return bk_timer_stop(priv->id) == BK_OK ? OK : -EIO;
}

static bool bk7258_timer_active(struct bk7258_timer_s *priv)
{
  return s_timer_is_init &&
         (bk_timer_get_enable_status() & BIT(priv->id)) != 0;
}

static int bk7258_timer_getstatus(struct timer_lowerhalf_s *lower,
                                  struct timer_status_s *status)
{
  struct bk7258_timer_s *priv = (struct bk7258_timer_s *)lower;
  uint32_t end;
  uint32_t cnt;

  status->flags    = 0;
  status->timeout  = priv->timeout_us;
  status->timeleft = 0;

  if (priv->callback != NULL)
    {
      status->flags |= TCFLAGS_HANDLER;
    }

  if (bk7258_timer_active(priv))
    {
      status->flags |= TCFLAGS_ACTIVE;
      end = bk_timer_get_period(priv->id);
      cnt = bk_timer_get_cnt(priv->id);
      if (cnt < end)
        {
          status->timeleft = (end - cnt) / 26;
        }
    }

  return OK;
}

static int bk7258_timer_settimeout(struct timer_lowerhalf_s *lower,
                                   uint32_t timeout)
{
  struct bk7258_timer_s *priv = (struct bk7258_timer_s *)lower;

  if (timeout == 0 || timeout > BK7258_TIMER_MAX_US)
    {
      return -EINVAL;
    }

  priv->timeout_us = timeout;
  if (bk7258_timer_active(priv))
    {
      bk_timer_program(priv->id, bk_timer_end_count(timeout, TIMER_UNIT_US));
    }

  return OK;
}

static void bk7258_timer_setcallback(struct timer_lowerhalf_s *lower,
                                     tccb_t callback, void *arg)
{
  struct bk7258_timer_s *priv = (struct bk7258_timer_s *)lower;
  irqstate_t flags;

  /* up_irq_save: below the Armino headers enter_critical_section is no
   * longer the <nuttx/irq.h> macro, and the pair only has to keep the
   * group ISR from seeing a half-written callback/arg.
   */

  flags          = up_irq_save();
  priv->callback = callback;
  priv->arg      = arg;
  up_irq_restore(flags);
}

static int bk7258_timer_maxtimeout(struct timer_lowerhalf_s *lower,
                                   uint32_t *maxtimeout)
{
  *maxtimeout = BK7258_TIMER_MAX_US;
  return OK;
}

static const struct timer_ops_s g_bk7258_timer_ops =
{
  .start       = bk7258_timer_start,
  .stop        = bk7258_timer_stop,
  .getstatus   = bk7258_timer_getstatus,
  .settimeout  = bk7258_timer_settimeout,
  .setcallback = bk7258_timer_setcallback,
  .maxtimeout  = bk7258_timer_maxtimeout,
};

int bk7258_timer_initialize(void)
{
  struct bk7258_timer_s *priv = &g_bk7258_timer[BK7258_TIMER_DEV_ID];

  priv->lower.ops  = &g_bk7258_timer_ops;
  priv->id         = BK7258_TIMER_DEV_ID;
  priv->timeout_us = 10000;
  return timer_register("/dev/timer1", &priv->lower) == NULL ? -EIO : OK;
}

#endif /* CONFIG_BK7258_TIMER */
#endif /* !CONFIG_BK7258_COMPONENT_AP */

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
#include <os/os.h>

#include "bk_misc.h"
#include "sys_hal.h"
#include "timer_hal.h"

/* Timer registers are reached only through timer_hal / timer_ll below. */

#define BK_TIMER_CHAN_PER_GROUP   SOC_TIMER_CHAN_NUM_PER_GROUP
#define BK_TIMER_CHAN_NUM         SOC_TIMER_CHAN_NUM_PER_UNIT

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

/* The baseline's s_timer.hal.  Initialised statically because
 * bk_timer_delay_us() runs from the clock reprogram, before
 * bk_timer_driver_init().
 */

static timer_hal_t s_timer_hal =
{
  .hw = (timer_hw_t *)TIMER_LL_REG_BASE(0),
};

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

/* ctrl.timerN_int_en doubles as the pending flag on this block,
 * write-1-to-clear; timer_hal_get_interrupt_status() reads those bits.
 * Each group's handler serves its own three channels, as timer_isr /
 * timer1_isr do with SOC_TIMER_INTERRUPT_NUM > 1.
 */

static void bk_timer_group_isr(uint32_t g)
{
  uint32_t status = timer_hal_get_interrupt_status(&s_timer_hal);
  uint32_t chan;
  uint32_t c;

  for (c = 0; c < BK_TIMER_CHAN_PER_GROUP; c++)
    {
      chan = g * BK_TIMER_CHAN_PER_GROUP + c;
      if (!timer_hal_is_interrupt_triggered(&s_timer_hal, chan, status))
        {
          continue;
        }

      timer_hal_clear_chan_interrupt_status(&s_timer_hal, chan);
      if (g_bk7258_timer[chan].isr != NULL)
        {
          g_bk7258_timer[chan].isr(chan);
        }
    }
}

/* int_group_isr_t is void (*)(void), the baseline timer_isr's shape. */

static void bk_timer_group0_isr(void)
{
  bk_timer_group_isr(0);
}

static void bk_timer_group1_isr(void)
{
  bk_timer_group_isr(1);
}

/****************************************************************************
 * timer_hal.c face
 *
 * The baseline's HAL layer between bk_timer_* and timer_ll.h:
 * middleware/soc/common/hal/timer_hal.c (7 symbols) and
 * middleware/soc/bk7258/hal/timer_hal.c (timer_hal_delay_us here,
 * timer_hal_us_init further down).  Bodies follow the authority with
 * CONFIG_SYSTEM_CTRL=1.  s_timer_hal is initialised statically because
 * bk_timer_delay_us() runs from the clock reprogram, before
 * bk_timer_driver_init().
 ****************************************************************************/

#define TIMER0_PERIOD  0xffffffff

/* timer_s = counter_value * (1 / (freq / div)) */

uint32_t timer_hal_cal_end_count(timer_id_t chan, uint64_t time,
                                 uint32_t div, timer_value_unit_t unit_type)
{
  uint64_t value;
  uint16_t unit_factor;
  uint32_t timer_clock = TIMER_SCLK_XTAL;

  if (div == 0)
    {
      div = 1;
    }

  unit_factor = (unit_type == TIMER_UNIT_MS) ? 1 : 1000;

  switch (chan / SOC_TIMER_CHAN_NUM_PER_GROUP)
    {
      case 0:
        timer_clock = sys_hal_timer_select_clock_get(SYS_SEL_TIMER0);
        break;

      case 1:
        timer_clock = sys_hal_timer_select_clock_get(SYS_SEL_TIMER1);
        break;

      default:
        break;
    }

  if (timer_clock == TIMER_SCLK_XTAL)
    {
      value = time * TIMER_CLOCK_FREQ_26M / unit_factor / div;
    }
  else
    {
      value = time * TIMER_CLOCK_FREQ_32K / unit_factor / div;
    }

  if (value > 0xffffffff)
    {
      value = 0xffffffff;
    }

  return (uint32_t)value;
}

bk_err_t timer_hal_init(timer_hal_t *hal)
{
  int chan;

  hal->hw = (timer_hw_t *)TIMER_LL_REG_BASE(hal->id);

  for (chan = 0; chan < SOC_TIMER_CHAN_NUM_PER_UNIT; chan++)
    {
      timer_ll_init(hal->hw, chan);
    }

  return BK_OK;
}

bk_err_t timer_hal_init_timer(timer_hal_t *hal, timer_id_t chan,
                              uint64_t time, timer_value_unit_t unit_type)
{
  uint32_t end_count = timer_hal_cal_end_count(chan, time, 1, unit_type);

  timer_ll_set_end_count(hal->hw, chan, end_count);
  timer_ll_set_clk_div(hal->hw, chan, 0);
  timer_ll_clear_chan_interrupt_status(hal->hw, chan);
  return BK_OK;
}

bk_err_t timer_hal_set_period(timer_hal_t *hal, timer_id_t chan,
                              uint32_t time_ms)
{
  uint32_t end_count = timer_hal_cal_end_count(chan, time_ms, 1,
                                               TIMER_UNIT_MS);

  timer_ll_set_end_count(hal->hw, chan, end_count);
  return BK_OK;
}

bk_err_t timer_hal_start_common(timer_hal_t *hal, timer_id_t chan)
{
  timer_ll_enable(hal->hw, chan);
  return BK_OK;
}

bk_err_t timer_hal_stop_common(timer_hal_t *hal, timer_id_t chan)
{
  timer_ll_disable_interrupt(hal->hw, chan);
  timer_ll_clear_chan_interrupt_status(hal->hw, chan);
  timer_ll_disable(hal->hw, chan);
  return BK_OK;
}

uint32_t timer_hal_get_count(timer_hal_t *hal, timer_id_t chan)
{
  uint32_t en_status = timer_ll_get_enable_status(hal->hw);

  if (!(en_status & BIT(chan)))
    {
      return 0;
    }

  timer_ll_set_read_index(hal->hw, chan);
  timer_ll_set_cnt_read(hal->hw, chan);

  /* Wait for the hardware to latch the count. */

  BK_WHILE (!timer_ll_is_cnt_read_valid(hal->hw, chan));

  return timer_ll_get_timer_count(hal->hw, chan);
}

/* soc/bk7258/hal/timer_hal.c: channel 0 as a free-running 26 MHz counter. */

static inline uint32_t timer_hal_get_timer0_cnt(void)
{
  volatile uint32_t int_level;
  uint32_t cnt;

  int_level = rtos_enter_critical();
  timer_ll_set_read_index(s_timer_hal.hw, 0);
  timer_ll_set_cnt_read(s_timer_hal.hw, 0);
  while (!timer_ll_is_cnt_read_valid(s_timer_hal.hw, 0))
    {
    }

  cnt = timer_ll_get_timer_count(s_timer_hal.hw, 0);
  rtos_exit_critical(int_level);
  return cnt;
}

/* Kept as the authority wrote it: end == begin reads as a full period, so
 * the loop exits.  Two latched reads of a 26 MHz counter are never equal in
 * practice, since each read waits for the latch.
 */

static inline uint32_t timer_hal_diff(uint32_t begin, uint32_t end)
{
  if (end > begin)
    {
      return end - begin;
    }

  return TIMER0_PERIOD - begin + end;
}

void timer_hal_delay_us(uint32_t us)
{
  uint32_t delay_cycle = 26 * us;
  uint32_t begin;
  uint32_t end;
  uint32_t diff;

  if (!(timer_ll_get_enable_status(s_timer_hal.hw) & BIT(0)))
    {
      timer_ll_enable(s_timer_hal.hw, 0);
    }

  begin = timer_hal_get_timer0_cnt();
  do
    {
      end  = timer_hal_get_timer0_cnt();
      diff = timer_hal_diff(begin, end);
    }
  while (diff < delay_cycle);
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

  /* timer_chan_deinit_common for every channel, supported or not: stop,
   * reset the config to default, and gate the group clock back off.  isr
   * is cleared here because bk_timer_driver_init() does not memset it.
   */

  for (chan = 0; chan < BK_TIMER_CHAN_NUM; chan++)
    {
      timer_hal_stop_common(&s_timer_hal, chan);
      timer_hal_reset_config_to_default(&s_timer_hal, chan);
      g_bk7258_timer[chan].isr        = NULL;
      g_bk7258_timer[chan].timeout_us = 0;
    }

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN, 0);
  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER1_CLK_EN, 0);

  s_timer_is_init = false;
  return BK_OK;
}

static void bk_timer_chan_init_common(timer_id_t timer_id)
{
  uint32_t g = bk_timer_group(timer_id);

  /* timer_chan_init_common: pick XTAL and gate the group clock on.  Both are
   * per group, so TIMER_ID3..5 need this too -- only doing group 0 left
   * group 1's clock off.  The SYS block is not the timer's; these stay on
   * its registers until T12 brings sys_driver over.
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

}

/* timer_chan_enable_interrupt_common: the ICU-level route first, then the
 * peripheral bit.  Missing the ICU step is what produced
 * irq_unexpected_isr -- the block fired onto a line nothing had attached.
 */

static void bk_timer_chan_enable_interrupt_common(timer_id_t timer_id)
{
  modifyreg32(BK7258_SYS_CPU0_INT_EN, 0,
              bk_timer_group(timer_id) == 0 ? BK_TIMER0_ICU_BIT :
                                              BK_TIMER1_ICU_BIT);
  timer_hal_enable_interrupt(&s_timer_hal, timer_id);
}

/* bk_timer_start_without_callback()'s sequence, in the authority's order,
 * shared with the NuttX face so both program a channel the same way.
 * Callers have done the id and init checks.
 */

static void bk_timer_program(timer_id_t timer_id, uint64_t time,
                             timer_value_unit_t unit)
{
  bk_timer_chan_init_common(timer_id);
  bk_timer_chan_enable_interrupt_common(timer_id);

  /* timer_driver.c:365: a running channel is stopped and given delay(4)
   * before reprogramming -- a counter already past the new end count would
   * never reach it again.
   */

  if (timer_hal_get_enable_status(&s_timer_hal) & BIT(timer_id))
    {
      timer_hal_disable(&s_timer_hal, timer_id);
      delay(4);
    }

  timer_hal_init_timer(&s_timer_hal, timer_id, time, unit);
  timer_hal_start_common(&s_timer_hal, timer_id);
}

bk_err_t bk_timer_start_without_callback(timer_id_t timer_id, uint32_t time_ms)
{
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  bk_timer_program(timer_id, time_ms, TIMER_UNIT_MS);
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
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  timer_hal_stop_common(&s_timer_hal, timer_id);
  return BK_OK;
}

bk_err_t bk_timer_cancel(timer_id_t timer_id)
{
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  timer_hal_reset_config_to_default(&s_timer_hal, timer_id);
  g_bk7258_timer[timer_id].isr        = NULL;
  g_bk7258_timer[timer_id].timeout_us = 0;
  return BK_OK;
}

uint32_t bk_timer_get_cnt(timer_id_t timer_id)
{
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  return timer_hal_get_count(&s_timer_hal, timer_id);
}

bk_err_t bk_timer_enable(timer_id_t timer_id)
{
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  timer_hal_enable(&s_timer_hal, timer_id);
  return BK_OK;
}

bk_err_t bk_timer_disable(timer_id_t timer_id)
{
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  timer_hal_disable(&s_timer_hal, timer_id);
  return BK_OK;
}

uint32_t bk_timer_get_period(timer_id_t timer_id)
{
  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  return timer_hal_get_end_count(&s_timer_hal, timer_id);
}

uint32_t bk_timer_get_enable_status(void)
{
  TIMER_RETURN_ON_NOT_INIT();

  return timer_hal_get_enable_status(&s_timer_hal);
}

bool bk_timer_is_interrupt_triggered(timer_id_t timer_id)
{
  uint32_t int_status;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  int_status = timer_hal_get_interrupt_status(&s_timer_hal);
  return timer_hal_is_interrupt_triggered(&s_timer_hal, timer_id,
                                          int_status);
}

uint32_t timer_clear_isr_status(void)
{
  uint32_t int_status;

  int_status = timer_hal_get_interrupt_status(&s_timer_hal);
  timer_hal_clear_interrupt_status(&s_timer_hal, int_status);
  return int_status;
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
  /* global_ctrl.soft_reset, an all-ones end count, channel 0 on; then the
   * two SYS bits (clock gate, XTAL select), which belong to the SYS block.
   */

  timer_ll_soft_reset(s_timer_hal.hw, 0);
  timer_ll_set_end_count(s_timer_hal.hw, 0, TIMER0_PERIOD);
  timer_ll_enable(s_timer_hal.hw, 0);

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_TIMER0_CLK_EN,
              BK7258_SYS_TIMER0_CLK_EN);
  modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_TIMER0_XTAL,
              BK7258_SYS_TIMER0_XTAL);
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

  uint32_t timer_clock = TIMER_SCLK_XTAL;

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);

  current_count = (uint64_t)timer_hal_get_count(&s_timer_hal, timer_id) +
                  last_count;

  if (div == 0)
    {
      div = 1;
    }

  switch (timer_id / SOC_TIMER_CHAN_NUM_PER_GROUP)
    {
      case 0:
        timer_clock = sys_hal_timer_select_clock_get(SYS_SEL_TIMER0);
        break;

      case 1:
        timer_clock = sys_hal_timer_select_clock_get(SYS_SEL_TIMER1);
        break;

      default:
        break;
    }

  unit_factor = (unit_type == TIMER_UNIT_MS) ? 1 : 1000;

  if (timer_clock == TIMER_SCLK_XTAL)
    {
      return unit_factor * current_count * (uint64_t)div /
             TIMER_CLOCK_FREQ_26M;
    }

  return unit_factor * current_count * (uint64_t)div / TIMER_CLOCK_FREQ_32K;
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

  TIMER_RETURN_ON_NOT_INIT();
  TIMER_RETURN_ON_INVALID_ID(timer_id);
  TIMER_RETURN_ON_UNSUPPORTED_ID(timer_id);

  bk_timer_chan_init_common(timer_id);
  bk_timer_chan_enable_interrupt_common(timer_id);

  /* The end count is absolute and truncated to 32 bits, as the authority
   * does: the counter wraps, so truncation is the wrap.
   */

  current_count = timer_hal_get_count(&s_timer_hal, timer_id);
  delta_count   = timer_hal_cal_end_count(timer_id, time_us, 1,
                                          TIMER_UNIT_US);
  end_count     = current_count + delta_count;

  timer_ll_set_end_count(s_timer_hal.hw, timer_id, (uint32_t)end_count);
  timer_ll_set_clk_div(s_timer_hal.hw, timer_id, 0);
  timer_ll_clear_chan_interrupt_status(s_timer_hal.hw, timer_id);
  timer_hal_start_common(&s_timer_hal, timer_id);

  g_bk7258_timer[timer_id].isr = callback;
  return BK_OK;
}

void bk_timer_delay_us(uint32_t us)
{
  timer_hal_delay_us(us);
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
      bk_timer_program(timer_id, next_us, TIMER_UNIT_US);
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
  bk_timer_program(priv->id, priv->timeout_us, TIMER_UNIT_US);
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
      bk_timer_program(priv->id, timeout, TIMER_UNIT_US);
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

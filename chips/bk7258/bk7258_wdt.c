/****************************************************************************
 * chips/bk7258/bk7258_wdt.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Armino WDT face
 *
 * The Armino modules call the names declared in <driver/wdt.h>,
 * <driver/aon_wdt.h>, wdt_hal.h and aon_wdt_hal.h.  Until now they resolved
 * to four units of the baseline:
 *
 *   middleware/driver/wdt/wdt_driver.c          (17 symbols)
 *   middleware/driver/aon_wdt/aon_wdt_driver.c  ( 5 symbols)
 *   middleware/soc/common/hal/wdt_hal.c         ( 5 symbols)
 *   middleware/soc/common/hal/aon_wdt_hal.c     ( 3 symbols)
 *
 * This file provides the same 30 symbols.  What makes it a port rather than
 * a rewrite:
 *
 *   - the signatures come from the authority headers (never re-declared
 *     here), so a caller cannot drift;
 *   - the #if gates are the authority's own, evaluated against the vendor
 *     sdkconfig.h the baseline units are compiled with, so flipping a config
 *     moves the gate and its behaviour together;
 *   - the register layer is the authority's wdt_ll.h / aon_wdt_ll.h
 *     (static inline): the key sequences, the soft_reset-before-period
 *     ordering and the clock-divider scaling are not re-typed;
 *   - the time base stays wall clock (bk_aon_rtc_get_us()/1000 when
 *     CONFIG_AON_RTC), NOT the NuttX scheduler tick: the task watchdog
 *     exists to notice that the scheduler stopped, so it must not share its
 *     clock.
 *
 * Symbols nothing references today (bk_wdt_driver_deinit,
 * bk_wdt_get_feed_time, bk_task_wdt_stop's siblings, ...) are implemented
 * anyway.  "The linker dropped it" is a fact about the current call graph,
 * not a licence to leave the contract incomplete.
 *
 * Two watchdogs live behind this face, both enabled in the baseline config:
 *
 *   CONFIG_INT_WDT   period 8000 ms   hardware, fed every 500 ms from a
 *                                     1 s TIMER_ID2 callback
 *   CONFIG_TASK_WDT  period 16000 ms  software, BK_ASSERT(0) on timeout
 *
 * The task watchdog is inert until the first bk_task_wdt_feed() (see the
 * s_last_task_wdt_feed_tick guard).  That inertness is part of the contract:
 * arming it on start would turn a quiet board into one that asserts every
 * 16 s.
 *
 * CP only: the baseline include directories this file needs are added for
 * the CP component (chips/bk7258/CMakeLists.txt).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <errno.h>

#include <nuttx/clock.h>

#include <os/os.h>
#include <os/mem.h>

#include <driver/timer.h>
#include <driver/aon_rtc.h>
#include <driver/gpio.h>
#include <components/system.h>

#include "wdt_hal.h"
#include "aon_wdt_hal.h"
#include "wdt_driver.h"
#include "bk_wdt.h"
#include "aon_wdt_driver.h"
#include <driver/wdt.h>
#include <driver/aon_wdt.h>
#include "reset_reason.h"
#include "aon_pmu_driver.h"
#include "sys_driver.h"
#include "bk_fake_clock.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Copied verbatim from wdt_driver.c: the feed cadence is derived from the
 * hardware period, not configured separately.  BK_MS_TO_TICKS() is
 * x / rtos_get_ms_per_tick(), and the baseline builds 1000 Hz ticks, so one
 * tick is one millisecond.
 */

#define INT_WDG_FEED_PERIOD_TICK ((BK_MS_TO_TICKS(CONFIG_INT_WDT_PERIOD_MS)) >> 4)
#define TASK_WDG_PERIOD_TICK     (BK_MS_TO_TICKS(CONFIG_TASK_WDT_PERIOD_MS))

#define WDT_BARK_TIME_MS         1000
#define NMI_WDT_CLK_DIV_16       3

#if CONFIG_AON_RTC
#  define GET_TASK_CURRENT_TICK() (BK_MS_TO_TICKS(bk_aon_rtc_get_us() / 1000))
#else
#  define GET_TASK_CURRENT_TICK() (bk_get_tick())
#endif

#define WDT_RETURN_ON_DRIVER_NOT_INIT() \
  do \
    { \
      if (!s_wdt_driver_is_init) \
        { \
          WDT_LOGE("WDT driver not init\r\n"); \
          return BK_ERR_WDT_DRIVER_NOT_INIT; \
        } \
    } \
  while (0)

#define WDT_RETURN_ON_NOT_INIT() \
  do \
    { \
      if (!(s_wdt.init_bits & BIT(0))) \
        { \
          return BK_ERR_WDT_NOT_INIT; \
        } \
    } \
  while (0)

#define WDT_RETURN_ON_INVALID_PERIOD(timeout) \
  do \
    { \
      if ((timeout) > WDT_F_PERIOD_V) \
        { \
          WDT_LOGW("WDT invalid timeout\r\n"); \
          return BK_ERR_WDT_INVALID_PERIOD; \
        } \
    } \
  while (0)

#define AON_WDT_RETURN_ON_INVALID_PERIOD(timeout) \
  do \
    { \
      if ((timeout) > AON_WDT_F_PERIOD_V) \
        { \
          AON_WDT_LOGE("AON_WDT invalid timeout\r\n"); \
          return BK_ERR_AON_WDT_INVALID_PERIOD; \
        } \
    } \
  while (0)

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  wdt_hal_t hal;
  uint8_t   init_bits;
} wdt_driver_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static wdt_driver_t s_wdt = {0};
static bool         s_wdt_driver_is_init = false;
static uint32_t     s_wdt_period = CONFIG_INT_WDT_PERIOD_MS;

#if CONFIG_TASK_WDT
static uint64_t s_last_task_wdt_feed_tick = 0;
static uint64_t s_last_task_wdt_log_tick = 0;
static bool     s_task_wdt_enabled = true;
#endif

#if CONFIG_INT_WDT
static uint32_t s_feed_watchdog_time = INT_WDG_FEED_PERIOD_TICK;
#endif

static uint32_t s_aon_wdt_period = CONFIG_INT_AON_WDT_PERIOD_MS;

#if CONFIG_INT_AON_WDT
static uint32_t s_feed_aon_watchdog_time = INT_AON_WDG_FEED_PERIOD_TICK;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

__IRAM_SEC static void wdt_init_common(void)
{
#if (CONFIG_SYSTEM_CTRL)
  sys_drv_dev_clk_pwr_up(CLK_PWR_ID_WDG_CPU, CLK_PWR_CTRL_PWR_UP);
#else
  power_up_wdt();
#endif
}

__attribute__((section(".itcm_sec_code"))) static void wdt_deinit_common(void)
{
  s_wdt_period = CONFIG_INT_WDT_PERIOD_MS;
  wdt_hal_reset_config_to_default(&s_wdt.hal);
#if (CONFIG_SYSTEM_CTRL)
  close_wdt();
  /* sys_drv_dev_clk_pwr_up(CLK_PWR_ID_WDG_CPU, CLK_PWR_CTRL_PWR_DOWN); */
#else
  power_down_wdt();
#endif
}

static void aon_wdt_deinit_common(void)
{
  s_aon_wdt_period = CONFIG_INT_AON_WDT_PERIOD_MS;
#if (CONFIG_SYSTEM_CTRL)
  aon_wdt_hal_stop_aon_wdt();
#endif
}

/****************************************************************************
 * wdt_driver.c face
 ****************************************************************************/

bk_err_t bk_wdt_driver_init(void)
{
  if (s_wdt_driver_is_init)
    {
      return BK_OK;
    }

  os_memset(&s_wdt, 0, sizeof(s_wdt));
  wdt_hal_init(&s_wdt.hal);

  bk_timer_start(TIMER_ID2, WDT_BARK_TIME_MS, (timer_isr_t)bk_wdt_feed_handle);
  sys_drv_nmi_wdt_set_clk_div(NMI_WDT_CLK_DIV_16);
  aon_pmu_drv_wdt_rst_dev_enable();

  s_wdt_driver_is_init = true;

  return BK_OK;
}

bk_err_t bk_wdt_driver_deinit(void)
{
  if (!s_wdt_driver_is_init)
    {
      return BK_OK;
    }

  wdt_deinit_common();

  bk_timer_stop(TIMER_ID2);

  s_wdt_driver_is_init = false;

  return BK_OK;
}

/* 1. power up wdt
 * 2. set period
 */

__IRAM_SEC bk_err_t bk_wdt_start(uint32_t timeout_ms)
{
  WDT_RETURN_ON_DRIVER_NOT_INIT();
  WDT_RETURN_ON_INVALID_PERIOD(timeout_ms);

  if (!timeout_ms)
    {
      timeout_ms = CONFIG_INT_WDT_PERIOD_MS;
    }

  s_wdt_period = timeout_ms;
  wdt_init_common();
  wdt_hal_init_wdt(&s_wdt.hal, timeout_ms);
  s_wdt.init_bits |= BIT(0);
  WDT_LOGV("bk_wdt_start, s_wdt.init_bits:%x\n", s_wdt.init_bits);

  return BK_OK;
}

__attribute__((section(".itcm_sec_code"))) bk_err_t bk_wdt_stop(void)
{
  WDT_RETURN_ON_DRIVER_NOT_INIT();
  wdt_deinit_common();
  s_wdt.init_bits &= ~BIT(0);
  /* WDT_LOGV("bk_wdt_stop, s_wdt.init_bits:%x\n", s_wdt.init_bits); */
  return BK_OK;
}

bk_err_t bk_wdt_feed(void)
{
  WDT_RETURN_ON_DRIVER_NOT_INIT();
  WDT_RETURN_ON_NOT_INIT();

  wdt_hal_init_wdt(&s_wdt.hal, s_wdt_period);

  return BK_OK;
}

#if (CONFIG_INT_WDT)
void bk_int_wdt_feed(void)
{
  static uint64_t s_last_int_wdt_feed_tick = 0;
  uint64_t current_tick = GET_TASK_CURRENT_TICK();

  if ((current_tick - s_last_int_wdt_feed_tick) >= s_feed_watchdog_time)
    {
      bk_wdt_feed();
      s_last_int_wdt_feed_tick = current_tick;
      /* WDT_LOGV("feed interrupt watchdog, s_feed_watchdog_time = %u ms.\n",
       *          s_feed_watchdog_time); */
    }
}

uint32_t bk_wdt_get_feed_time(void)
{
  return s_feed_watchdog_time;
}

void bk_wdt_set_feed_time(uint32_t dw_set_time)
{
  s_feed_watchdog_time = dw_set_time;
}
#endif

#if (CONFIG_TASK_WDT)
__IRAM_SEC void bk_task_wdt_start(void)
{
  s_task_wdt_enabled = true;
}

__attribute__((section(".itcm_sec_code"))) void bk_task_wdt_stop(void)
{
  s_task_wdt_enabled = false;
}

void bk_task_wdt_feed(void)
{
  s_last_task_wdt_feed_tick = GET_TASK_CURRENT_TICK();
}

void bk_task_wdt_timeout_check(void)
{
  if (s_last_task_wdt_feed_tick && s_task_wdt_enabled)
    {
      const uint64_t c_last_feed_tick = s_last_task_wdt_feed_tick;
      const uint64_t current_tick = GET_TASK_CURRENT_TICK();

      if (current_tick > c_last_feed_tick)
        {
          if ((current_tick - c_last_feed_tick) > TASK_WDG_PERIOD_TICK)
            {
              if ((current_tick - s_last_task_wdt_log_tick) >
                  TASK_WDG_PERIOD_TICK)
                {
                  WDT_LOGW("task watchdog triggered\r\n");
                  s_last_task_wdt_log_tick = current_tick;
                  BK_ASSERT(0);
                }
            }
        }
    }
}
#endif

bool bk_wdt_is_driver_inited(void)
{
  return s_wdt_driver_is_init;
}

void bk_wdt_feed_handle(void)
{
  GLOBAL_INT_DECLARATION();
  GLOBAL_INT_DISABLE();

#if (CONFIG_INT_WDT)
  bk_int_wdt_feed();
#endif

#if (CONFIG_TASK_WDT)
  bk_task_wdt_timeout_check();
#endif
  GLOBAL_INT_RESTORE();
}

__attribute__((section(".itcm_sec_code"))) void close_wdt(void)
{
  wdt_hal_close();
}

void bk_wdt_force_feed(void)
{
  wdt_hal_force_feed();
}

void bk_wdt_force_reboot(void)
{
  GLOBAL_INT_DECLARATION();
  GLOBAL_INT_DISABLE();
  wdt_hal_force_reboot();
#if CONFIG_GPIO_RETENTION_SUPPORT
  /* lock gpio if retention map is set up
   * Attention: hot-flash-write will not work if gpio locked
   */

  if (0 != gpio_retention_map_get())
    {
      sys_hal_gpio_state_switch(true);
    }
#endif
  while (1);
  GLOBAL_INT_RESTORE();
}

/****************************************************************************
 * aon_wdt_driver.c face
 ****************************************************************************/

uint32_t bk_aon_wdt_get_period(void)
{
  return aon_wdt_hal_get_period();
}

bk_err_t bk_aon_wdt_set_period(uint32_t timeout_ms)
{
  AON_WDT_RETURN_ON_INVALID_PERIOD(timeout_ms);

  if (!timeout_ms)
    {
      timeout_ms = CONFIG_INT_AON_WDT_PERIOD_MS;
    }

  s_aon_wdt_period = timeout_ms;
  aon_wdt_hal_set_period(timeout_ms);
  AON_WDT_LOGV("bk_aon_wdt set period: %d.\r\n", timeout_ms);

  return BK_OK;
}

bk_err_t bk_aon_wdt_stop(void)
{
  aon_wdt_deinit_common();
  AON_WDT_LOGV("bk_aon_wdt_stop.\r\n");

  return BK_OK;
}

bk_err_t bk_aon_wdt_feed(void)
{
  aon_wdt_hal_set_period(s_aon_wdt_period);

  return BK_OK;
}

#if (CONFIG_INT_AON_WDT)
void bk_int_aon_wdt_feed(void)
{
  static uint64_t s_last_int_aon_wdt_feed_tick = 0;
  uint64_t current_tick = bk_get_tick();

  if ((current_tick - s_last_int_aon_wdt_feed_tick) >=
      s_feed_aon_watchdog_time)
    {
      bk_aon_wdt_feed();
      s_last_int_aon_wdt_feed_tick = current_tick;
      AON_WDT_LOGV("feed interrupt watchdog\n");
    }
}

uint32_t bk_aon_wdt_get_feed_time(void)
{
  return s_feed_aon_watchdog_time;
}

bk_err_t bk_aon_wdt_set_feed_time(uint32_t feed_time)
{
  s_feed_aon_watchdog_time = feed_time;
  return BK_OK;
}
#endif /* CONFIG_INT_AON_WDT */

void bk_aon_wdt_feed_handle(void)
{
  GLOBAL_INT_DECLARATION();
  GLOBAL_INT_DISABLE();

#if (CONFIG_INT_AON_WDT)
  bk_int_aon_wdt_feed();
#endif

  GLOBAL_INT_RESTORE();
}

/****************************************************************************
 * aon_wdt_hal.c face
 ****************************************************************************/

uint32_t aon_wdt_hal_get_period(void)
{
  return aon_wdt_ll_get_wd_period();
}

bk_err_t aon_wdt_hal_set_period(uint32_t timeout)
{
  aon_wdt_ll_set_period(timeout);
  return BK_OK;
}

bk_err_t aon_wdt_hal_stop_aon_wdt(void)
{
  aon_wdt_ll_set_period(0);
  return BK_OK;
}

/****************************************************************************
 * wdt_hal.c face
 ****************************************************************************/

static inline void wdt_hal_close_unused(wdt_unit_t id);

bk_err_t wdt_hal_init(wdt_hal_t *hal)
{
#if (CONFIG_SOC_BK7256XX)
#if CONFIG_DEBUG_VERSION || CONFIG_NMI_WDT_EN
  hal->id = NMI_WDT_ID;  /* debug version use nmi_wdt to dump */
#else
  if (aon_pmu_hal_is_chipid_later_than_version_C())
    {
      hal->id = AON_WDT_ID;  /* chip version C -> aon_wdt */
    }
  else
    {
      hal->id = NMI_WDT_ID;  /* chip version A -> nmi_wdt */
    }
#endif
#endif

#if (CONFIG_SOC_BK7236XX)
#if CONFIG_DEBUG_VERSION || CONFIG_NMI_WDT_EN
  hal->id = NMI_WDT_ID;  /* debug version use nmi_wdt to dump */
#else
  hal->id = AON_WDT_ID;  /* non-debug uses aon_wdt */
#endif
#endif

  wdt_hal_close_unused(hal->id);

  hal->hw = (wdt_hw_t *)WDT_LL_REG_BASE(hal->id);
  wdt_ll_init(hal->hw);
  return BK_OK;
}

__IRAM_SEC bk_err_t wdt_hal_init_wdt(wdt_hal_t *hal, uint32_t timeout)
{
  wdt_ll_set_period(hal->hw, timeout);
  return BK_OK;
}

#if (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || \
    (CONFIG_SOC_BK7286XX)

__attribute__((section(".itcm_sec_code"))) void wdt_hal_close(void)
{
#if !CONFIG_AON_WDT
  REG_WRITE(SOC_AON_WDT_REG_BASE, 0x5A0000);
  REG_WRITE(SOC_AON_WDT_REG_BASE, 0xA50000);
#endif
  REG_SET(SOC_WDT_REG_BASE + 4 * 2, 1, 1, 1);
  REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0x5A0000);
  REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0xA50000);
}

void wdt_hal_force_feed(void)
{
  REG_WRITE(SOC_AON_WDT_REG_BASE, 0x5AFFFC);
  REG_WRITE(SOC_AON_WDT_REG_BASE, 0xA5FFFC);

  REG_SET(SOC_WDT_REG_BASE + 4 * 2, 1, 1, 1);
  REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0x5AFFF0);
  REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0xA5FFF0);
}

static inline void wdt_hal_close_unused(wdt_unit_t id)
{
  if (id == AON_WDT_ID)
    {
      /* use aon_wdt, close nmi_wdt */

      REG_SET(SOC_WDT_REG_BASE + 4 * 2, 1, 1, 1);
      REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0x5A0000);
      REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0xA50000);
    }
  else
    {
      /* use nmi_wdt, close aon_wdt */

      REG_WRITE(SOC_AON_WDT_REG_BASE, 0x5A0000);
      REG_WRITE(SOC_AON_WDT_REG_BASE, 0xA50000);
    }
}

static inline void wdt_hal_nmi_reboot(void)
{
  set_reboot_tag(REBOOT_TAG_REQ);
  aon_pmu_drv_wdt_change_not_rosc_clk();
  aon_pmu_drv_wdt_rst_dev_enable();

  REG_SET(SOC_WDT_REG_BASE + 4 * 2, 1, 1, 1);
  REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0x5A000A);
  REG_WRITE(SOC_WDT_REG_BASE + 4 * 4, 0xA5000A);
}

/* Not inlined on purpose.  In the baseline this function lived in its own
 * translation unit (wdt_hal.c) and was called from wdt_driver.c, so the
 * out-of-line body was referenced and survived --gc-sections.  Merging the
 * four units into this file made the single caller (bk_wdt_force_reboot,
 * 20 lines below) easy to inline at -Os, and the image then lost the
 * symbol while the contract still had it.  noinline keeps the baseline's
 * cross-unit call shape, which is what makes the symbol observable in
 * nuttx.map.  One call per reset: no cost worth measuring.
 */

__attribute__((noinline)) void wdt_hal_force_reboot(void)
{
  /* disable AON WDT as the ROSC 32K need to be disabled */

#if CONFIG_DEBUG_VERSION || CONFIG_NMI_WDT_EN
  wdt_hal_nmi_reboot();
#else
  REG_WRITE(SOC_AON_WDT_REG_BASE, 0x5A000A);
  REG_WRITE(SOC_AON_WDT_REG_BASE, 0xA5000A);

#endif
}

#endif /* BK7236XX || BK7239XX || BK7286XX */

/****************************************************************************
 * NuttX watchdog face
 *
 * The same two watchdog units, seen through NuttX's watchdog framework.  This
 * is the fifth invariant in practice: the Armino symbols above stay the
 * compatibility surface for the PM / reset paths, /dev/watchdog0 is the
 * standard face, and both end at the same bk_wdt_* / aon_wdt_* bodies.
 *
 * What /dev/watchdog0 exposes is the interrupt watchdog (CONFIG_INT_WDT, 8 s
 * by default), because that is the one that can actually bite: the task
 * watchdog above is a software deadline checked by the 1 s timer hook.
 *
 * Caveat worth knowing before using it: bk7258_timer.c's 1 s hook already calls
 * bk_wdt_feed(), so the hardware is kept alive by the system regardless of this
 * device.  SETTIMEOUT changes the period, KEEPALIVE feeds immediately, and STOP
 * really disarms it -- the last one is why this device should be treated as a
 * diagnostic, not as something an application pets.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/timers/watchdog.h>

struct bk7258_wdg_s
{
  struct watchdog_lowerhalf_s lower;
  uint32_t timeout_ms;
  uint32_t last_feed;
};

static int bk7258_wdg_start(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdg_stop(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdg_keepalive(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdg_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status);
static int bk7258_wdg_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout);

static const struct watchdog_ops_s g_bk7258_wdg_ops =
{
  .start      = bk7258_wdg_start,
  .stop       = bk7258_wdg_stop,
  .keepalive  = bk7258_wdg_keepalive,
  .getstatus  = bk7258_wdg_getstatus,
  .settimeout = bk7258_wdg_settimeout,
};

static struct bk7258_wdg_s g_bk7258_wdg =
{
  .lower.ops  = &g_bk7258_wdg_ops,
  .timeout_ms = 8000,
};

static int bk7258_wdg_start(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdg_s *priv = (struct bk7258_wdg_s *)lower;

  priv->last_feed = clock_systime_ticks();
  return bk_wdt_start(priv->timeout_ms) == BK_OK ? OK : -EIO;
}

static int bk7258_wdg_stop(struct watchdog_lowerhalf_s *lower)
{
  return bk_wdt_stop() == BK_OK ? OK : -EIO;
}

static int bk7258_wdg_keepalive(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdg_s *priv = (struct bk7258_wdg_s *)lower;

  priv->last_feed = clock_systime_ticks();
  return bk_wdt_feed() == BK_OK ? OK : -EIO;
}

static int bk7258_wdg_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status)
{
  struct bk7258_wdg_s *priv = (struct bk7258_wdg_s *)lower;
  uint32_t elapsed;

  if (status == NULL)
    {
      return -EINVAL;
    }

  /* WDFLAGS_* is what the upper half reports; timeleft is what is left of the
   * period since our last feed, clamped at zero.
   */

  elapsed = (clock_systime_ticks() - priv->last_feed);

  status->flags     = WDFLAGS_ACTIVE | WDFLAGS_RESET;
  status->timeout   = priv->timeout_ms;
  status->timeleft  = elapsed >= priv->timeout_ms ? 0 : priv->timeout_ms -
                                                          elapsed;

  return OK;
}

static int bk7258_wdg_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout)
{
  struct bk7258_wdg_s *priv = (struct bk7258_wdg_s *)lower;

  if (timeout == 0)
    {
      return -EINVAL;
    }

  priv->timeout_ms = timeout;
  priv->last_feed  = clock_systime_ticks();

  return bk_wdt_start(timeout) == BK_OK ? OK : -EIO;
}

int bk7258_wdg_initialize(void)
{
  return watchdog_register("/dev/watchdog0", &g_bk7258_wdg.lower);
}

/****************************************************************************
 * Probe hook (see bk7258_probe.h)
 ****************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/timers/watchdog.h>

#include "bk7258_probe.h"

/* Walk the standard watchdog face: status, timeout, keepalive, status again.
 * STOP is deliberately not exercised -- it really disarms the hardware, and the
 * point of this probe is to show the device works, not to leave the board
 * unprotected.
 */

void bk7258_wdg_probe(void)
{
  struct watchdog_status_s st;
  int fd = open("/dev/watchdog0", O_RDWR);

  if (fd < 0)
    {
      printf("wdg: open(/dev/watchdog0) failed: %d\n", errno);
      return;
    }

  if (ioctl(fd, WDIOC_GETSTATUS, (unsigned long)&st) == 0)
    {
      printf("wdg: flags=0x%lx timeout=%lu timeleft=%lu\n",
             (unsigned long)st.flags, (unsigned long)st.timeout,
             (unsigned long)st.timeleft);
    }

  printf("wdg: SETTIMEOUT(10000) -> %d\n",
         ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)10000));

  if (ioctl(fd, WDIOC_GETSTATUS, (unsigned long)&st) == 0)
    {
      printf("wdg: after set timeout=%lu\n", (unsigned long)st.timeout);
    }

  printf("wdg: KEEPALIVE -> %d\n", ioctl(fd, WDIOC_KEEPALIVE, 0));

  /* Put the period back so the next boot's 8 s default is what the system
   * expects.
   */

  ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)8000);
  close(fd);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

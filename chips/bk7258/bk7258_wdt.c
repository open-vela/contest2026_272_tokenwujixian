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
#include <nuttx/timers/watchdog.h>

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
      if (!g_bk7258_wdg.driver_init) \
        { \
          WDT_LOGE("WDT driver not init\r\n"); \
          return BK_ERR_WDT_DRIVER_NOT_INIT; \
        } \
    } \
  while (0)

#define WDT_RETURN_ON_NOT_INIT() \
  do \
    { \
      if (!(g_bk7258_wdg.drv.init_bits & BIT(0))) \
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

/* The interrupt watchdog has one state owner: the NuttX lower half.  The
 * Armino bk_wdt_* face below reads and writes these same fields, so a direct
 * bk_wdt_stop() from pm or flash_driver_ext is what /dev/watchdog0 reports.
 * The fields are the baseline's s_wdt, s_wdt_driver_is_init and
 * s_wdt_period, plus the time of the last reload of the counter.
 */

struct bk7258_wdg_s
{
  struct watchdog_lowerhalf_s lower;
  wdt_driver_t                drv;
  bool                        driver_init;
  uint32_t                    period_ms;
  volatile uint32_t           feed_tick;  /* last feed, system ticks */
};

static struct bk7258_wdg_s g_bk7258_wdg =
{
  .period_ms = CONFIG_INT_WDT_PERIOD_MS,
};

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
  g_bk7258_wdg.period_ms = CONFIG_INT_WDT_PERIOD_MS;
  wdt_hal_reset_config_to_default(&g_bk7258_wdg.drv.hal);
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
  if (g_bk7258_wdg.driver_init)
    {
      return BK_OK;
    }

  os_memset(&g_bk7258_wdg.drv, 0, sizeof(g_bk7258_wdg.drv));
  wdt_hal_init(&g_bk7258_wdg.drv.hal);

  bk_timer_start(TIMER_ID2, WDT_BARK_TIME_MS, (timer_isr_t)bk_wdt_feed_handle);
  sys_drv_nmi_wdt_set_clk_div(NMI_WDT_CLK_DIV_16);
  aon_pmu_drv_wdt_rst_dev_enable();

  g_bk7258_wdg.driver_init = true;

  return BK_OK;
}

bk_err_t bk_wdt_driver_deinit(void)
{
  if (!g_bk7258_wdg.driver_init)
    {
      return BK_OK;
    }

  wdt_deinit_common();

  bk_timer_stop(TIMER_ID2);

  g_bk7258_wdg.driver_init = false;

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

  g_bk7258_wdg.period_ms = timeout_ms;
  wdt_init_common();
  wdt_hal_init_wdt(&g_bk7258_wdg.drv.hal, timeout_ms);
  g_bk7258_wdg.feed_tick = clock_systime_ticks();
  g_bk7258_wdg.drv.init_bits |= BIT(0);
  WDT_LOGV("bk_wdt_start, g_bk7258_wdg.drv.init_bits:%x\n", g_bk7258_wdg.drv.init_bits);

  return BK_OK;
}

__attribute__((section(".itcm_sec_code"))) bk_err_t bk_wdt_stop(void)
{
  WDT_RETURN_ON_DRIVER_NOT_INIT();
  wdt_deinit_common();
  g_bk7258_wdg.drv.init_bits &= ~BIT(0);
  /* WDT_LOGV("bk_wdt_stop, g_bk7258_wdg.drv.init_bits:%x\n", g_bk7258_wdg.drv.init_bits); */
  return BK_OK;
}

bk_err_t bk_wdt_feed(void)
{
  WDT_RETURN_ON_DRIVER_NOT_INIT();
  WDT_RETURN_ON_NOT_INIT();

  wdt_hal_init_wdt(&g_bk7258_wdg.drv.hal, g_bk7258_wdg.period_ms);
  g_bk7258_wdg.feed_tick = clock_systime_ticks();

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
  return g_bk7258_wdg.driver_init;
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

/* Every method reads and writes g_bk7258_wdg, the state the bk_wdt_* face
 * above also uses; none of them keeps a copy of its own.
 */

static int bk7258_wdg_start(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdg_s *priv = (struct bk7258_wdg_s *)lower;

  return bk_wdt_start(priv->period_ms) == BK_OK ? OK : -EIO;
}

static int bk7258_wdg_stop(struct watchdog_lowerhalf_s *lower)
{
  return bk_wdt_stop() == BK_OK ? OK : -EIO;
}

static int bk7258_wdg_keepalive(struct watchdog_lowerhalf_s *lower)
{
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

  /* ACTIVE follows the driver's own started bit, so a bk_wdt_stop() from
   * pm or flash_driver_ext shows up here.  timeleft is what is left of the
   * period since the counter was last reloaded, clamped at zero.
   */

  elapsed = clock_systime_ticks() - priv->feed_tick;

  status->flags    = WDFLAGS_RESET;
  if (priv->drv.init_bits & BIT(0))
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  status->timeout  = priv->period_ms;
  status->timeleft = 0;

  /* A stopped watchdog is not counting down. */

  if ((status->flags & WDFLAGS_ACTIVE) && elapsed < priv->period_ms)
    {
      status->timeleft = priv->period_ms - elapsed;
    }

  return OK;
}

static int bk7258_wdg_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout)
{
  if (timeout == 0)
    {
      return -EINVAL;
    }

  /* bk_wdt_start() records the period and reloads the counter. */

  return bk_wdt_start(timeout) == BK_OK ? OK : -EIO;
}

int bk7258_wdg_initialize(void)
{
  g_bk7258_wdg.lower.ops = &g_bk7258_wdg_ops;
  if (g_bk7258_wdg.feed_tick == 0)
    {
      g_bk7258_wdg.feed_tick = clock_systime_ticks();
    }

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

static void bk7258_wdg_probe_status(int fd, const char *when)
{
  struct watchdog_status_s st;

  if (ioctl(fd, WDIOC_GETSTATUS, (unsigned long)&st) == 0)
    {
      printf("wdg: %-22s active=%d timeout=%lu timeleft=%lu\n", when,
             (st.flags & WDFLAGS_ACTIVE) != 0, (unsigned long)st.timeout,
             (unsigned long)st.timeleft);
    }
  else
    {
      printf("wdg: %-22s GETSTATUS failed: %d\n", when, errno);
    }
}

/* Walk the standard watchdog face, then change the state through the Armino
 * face and read it back through /dev/watchdog0.  The second half is the
 * evidence that the two faces share one state: before 2026-10-23 the device
 * kept its own timeout and always reported ACTIVE.
 *
 * Expected: after "armino stop" active=0; after "armino start(default)"
 * active=1 and timeout=CONFIG_INT_WDT_PERIOD_MS, even though /dev set 10000
 * just before -- the same stop/start pair flash_driver_ext runs around an
 * erase.  The hardware is disarmed only between the two calls.
 */

void bk7258_wdg_probe(void)
{
  int fd = open("/dev/watchdog0", O_RDWR);

  if (fd < 0)
    {
      printf("wdg: open(/dev/watchdog0) failed: %d\n", errno);
      return;
    }

  bk7258_wdg_probe_status(fd, "initial");

  printf("wdg: SETTIMEOUT(10000) -> %d\n",
         ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)10000));
  bk7258_wdg_probe_status(fd, "after dev settimeout");

  printf("wdg: KEEPALIVE -> %d\n", ioctl(fd, WDIOC_KEEPALIVE, 0));

  printf("wdg: armino bk_wdt_stop -> %d\n", bk_wdt_stop());
  bk7258_wdg_probe_status(fd, "after armino stop");

  printf("wdg: armino bk_wdt_start(%d) -> %d\n", CONFIG_INT_WDT_PERIOD_MS,
         bk_wdt_start(CONFIG_INT_WDT_PERIOD_MS));
  bk7258_wdg_probe_status(fd, "after armino start");

  printf("wdg: dev STOP -> %d\n", ioctl(fd, WDIOC_STOP, 0));
  bk7258_wdg_probe_status(fd, "after dev stop");

  printf("wdg: dev START -> %d\n", ioctl(fd, WDIOC_START, 0));
  bk7258_wdg_probe_status(fd, "after dev start");

  close(fd);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

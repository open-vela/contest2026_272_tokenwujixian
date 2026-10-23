/****************************************************************************
 * chips/bk7258/bk7258_rtc.c
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
 * NuttX RTC lower half over the AON RTC counter
 *
 * The AON RTC is in the always-on domain, so its counter keeps running across
 * sleep; that is what makes it the right time base for a wall clock.  This
 * file exposes it as /dev/rtc0 through NuttX's RTC framework (rtc_ops_s), on
 * top of the nativized driver's bk_aon_rtc_get_us().
 *
 * This file is the Armino-compatible face's NuttX sibling.  The dependency runs
 * one way: the AON RTC counter access lives in the nativized driver
 * (bk_aon_rtc_get_us / bk_alarm_register), and both the framework ops here and
 * the Armino API the other modules call end at that same code.  The ops is not
 * a wrapper over a second implementation, and the driver is not a wrapper over
 * the ops -- there is one implementation with a NuttX-shaped front and an
 * Armino-shaped front, because the µs counter has no NuttX equivalent while
 * time-of-day and alarms do.
 *
 * No epoch is stored in hardware: the counter counts from power-on, and the
 * difference between it and the requested wall clock is kept in s_epoch_s.
 * That means the clock survives sleep but not a power cycle, which matches
 * what the Armino driver itself provides today.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <time.h>

#include <nuttx/arch.h>
#include <nuttx/timers/rtc.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <driver/aon_rtc.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rtc_lowerhalf_s
{
  struct rtc_lowerhalf_s lower;
};

/* The framework's alarm face.  bk_alarm_register() (the nativized driver) is the
 * engine: it owns the comparator, the tick ISR and the expiry walk, and calls
 * back here.  What this file adds is the registration the NuttX upper half
 * needs.
 *
 * BK7258 has a single AON RTC unit (AON_RTC_ID_2 only exists when
 * SOC_AON_RTC_UNIT_NUM > 1), and pm / low_pwr already put their wakeup alarm
 * on it.  That is fine: the driver keeps a named list per unit, so this alarm
 * is one more node next to theirs.
 */

struct bk7258_alarm_s
{
  rtc_alarm_callback_t cb;
  void *priv;
  struct rtc_time when;
  bool active;
};

#define BK7258_ALARM_UNIT   AON_RTC_ID_1
#define BK7258_ALARM_NAME   ((uint8_t *)"nuttx-rtc")

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Seconds added to the raw counter to make up the requested wall clock. */

static time_t s_epoch_s;

static struct bk7258_alarm_s s_alarm;

static int bk7258_rtc_arm(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_rtc_rdtime(struct rtc_lowerhalf_s *lower,
                             struct rtc_time *rtctime)
{
  time_t now;
  struct tm tmv;

  if (rtctime == NULL)
    {
      return -EINVAL;
    }

  now = s_epoch_s + (time_t)(bk_aon_rtc_get_us() / 1000000u);

  /* Fill the calendar too: with CONFIG_RTC_DATETIME the framework converts
   * these fields itself, and without it only tm_sec is read.
   */

  gmtime_r(&now, &tmv);

  rtctime->tm_sec   = tmv.tm_sec;
  rtctime->tm_min   = tmv.tm_min;
  rtctime->tm_hour  = tmv.tm_hour;
  rtctime->tm_mday  = tmv.tm_mday;
  rtctime->tm_mon   = tmv.tm_mon;
  rtctime->tm_year  = tmv.tm_year;
  rtctime->tm_wday  = tmv.tm_wday;
  rtctime->tm_yday  = tmv.tm_yday;
  rtctime->tm_isdst = tmv.tm_isdst;

  return OK;
}

static int bk7258_rtc_settime(struct rtc_lowerhalf_s *lower,
                              const struct rtc_time *rtctime)
{
  uint64_t us;

  if (rtctime == NULL)
    {
      return -EINVAL;
    }

  us = bk_aon_rtc_get_us();

  /* Only the epoch moves; the counter itself is never written. */

  s_epoch_s = (time_t)rtctime->tm_sec - (time_t)(us / 1000000u);

  /* A pending alarm is an absolute instant, so it has to follow the clock. */

  if (s_alarm.active)
    {
      return bk7258_rtc_arm();
    }

  return OK;
}

static bool bk7258_rtc_havesettime(struct rtc_lowerhalf_s *lower)
{
  return true;
}

/* Called from the AON RTC tick ISR, in interrupt context. */

static void bk7258_rtc_alarm_fired(aon_rtc_id_t id, uint8_t *name,
                                   void *param)
{
  rtc_alarm_callback_t cb = s_alarm.cb;
  void *priv = s_alarm.priv;

  s_alarm.cb     = NULL;
  s_alarm.priv   = NULL;
  s_alarm.active = false;

  if (cb != NULL)
    {
      cb(priv, 0);
    }
}

static time_t bk7258_rtc_time2sec(const struct rtc_time *rt)
{
  struct tm tmv;

  memset(&tmv, 0, sizeof(tmv));
  tmv.tm_sec  = rt->tm_sec;
  tmv.tm_min  = rt->tm_min;
  tmv.tm_hour = rt->tm_hour;
  tmv.tm_mday = rt->tm_mday;
  tmv.tm_mon  = rt->tm_mon;
  tmv.tm_year = rt->tm_year;

  return timegm(&tmv);
}

/* Program whatever s_alarm.when asks for, measured from the current counter.
 * Called by setalarm() and again by settime(), because moving the wall clock
 * must move a pending alarm with it: the entry point is absolute time, so the
 * delta the hardware carries has to be recomputed.
 */

static int bk7258_rtc_arm(void)
{
  alarm_info_t info;
  time_t now;
  time_t when;
  int ret;

  now  = s_epoch_s + (time_t)(bk_aon_rtc_get_us() / 1000000u);
  when = bk7258_rtc_time2sec(&s_alarm.when);

  if (when <= now)
    {
      s_alarm.active = false;
      return -EINVAL;
    }

  memset(&info, 0, sizeof(info));

  /* One shot: the driver's alarm list walks the node out when it expires;
   * period_cnt 1 keeps it from re-arming.
   */

  memcpy(info.name, BK7258_ALARM_NAME, strlen((char *)BK7258_ALARM_NAME));
  info.period_tick = (rtc_tick_t)(when - now) * bk_rtc_get_clock_freq();
  info.period_cnt  = 1;
  info.callback    = bk7258_rtc_alarm_fired;

  /* Re-register under the same name so a second SET_ALARM replaces the first. */

  bk_alarm_unregister(BK7258_ALARM_UNIT, BK7258_ALARM_NAME);
  ret = bk_alarm_register(BK7258_ALARM_UNIT, &info);
  if (ret != BK_OK)
    {
      s_alarm.active = false;
      return -EIO;
    }

  s_alarm.active = true;
  return OK;
}

static int bk7258_rtc_setalarm(struct rtc_lowerhalf_s *lower,
                               const struct lower_setalarm_s *alarminfo)
{
  if (alarminfo == NULL)
    {
      return -EINVAL;
    }

  s_alarm.cb   = alarminfo->cb;
  s_alarm.priv = alarminfo->priv;
  s_alarm.when = alarminfo->time;

  return bk7258_rtc_arm();
}

static int bk7258_rtc_setrelative(struct rtc_lowerhalf_s *lower,
                                  const struct lower_setrelative_s *alarminfo)
{
  struct lower_setalarm_s abs;
  struct rtc_time rt;
  time_t when;
  int ret;

  if (alarminfo == NULL)
    {
      return -EINVAL;
    }

  when = s_epoch_s + (time_t)(bk_aon_rtc_get_us() / 1000000u) +
         alarminfo->reltime;
  gmtime_r(&when, (struct tm *)&abs.time);

  abs.id   = alarminfo->id;
  abs.cb   = alarminfo->cb;
  abs.priv = alarminfo->priv;
  ret      = bk7258_rtc_setalarm(lower, &abs);
  (void)rt;

  return ret;
}

static int bk7258_rtc_rdalarm(struct rtc_lowerhalf_s *lower,
                              struct lower_rdalarm_s *alarminfo)
{
  if (alarminfo == NULL || alarminfo->time == NULL)
    {
      return -EINVAL;
    }

  *alarminfo->time = s_alarm.when;
  return s_alarm.active ? OK : -ENOENT;
}

static int bk7258_rtc_cancelalarm(struct rtc_lowerhalf_s *lower, int alarmid)
{
  s_alarm.cb     = NULL;
  s_alarm.priv   = NULL;
  s_alarm.active = false;

  bk_alarm_unregister(BK7258_ALARM_UNIT, BK7258_ALARM_NAME);
  return OK;
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rtc_ops_s g_bk7258_rtc_ops =
{
  .rdtime      = bk7258_rtc_rdtime,
  .settime     = bk7258_rtc_settime,
  .havesettime = bk7258_rtc_havesettime,
#ifdef CONFIG_RTC_ALARM
  .setalarm     = bk7258_rtc_setalarm,
  .setrelative  = bk7258_rtc_setrelative,
  .rdalarm      = bk7258_rtc_rdalarm,
  .cancelalarm  = bk7258_rtc_cancelalarm,
#endif
};

static struct bk7258_rtc_lowerhalf_s g_bk7258_rtc =
{
  .lower.ops = &g_bk7258_rtc_ops,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* clock_initialize() asks the arch whether an RTC exists before it tries to
 * seed CLOCK_REALTIME.  The counter itself is started by
 * bk_aon_rtc_driver_init() in the Armino driver bring-up, which runs later, so
 * there is nothing to program here; reporting success only enables the path.
 * Until bk7258_rtc_initialize() has run, arch_rtc.c's g_rtc_lower is NULL and
 * up_rtc_getdatetime() answers -EAGAIN, which is what keeps the kernel base at
 * zero rather than reading a half-initialised block.
 */

int up_rtc_initialize(void)
{
  return OK;
}

int bk7258_rtc_initialize(void)
{
  int ret;

  /* First make it the system RTC: arch_rtc.c then answers up_rtc_time() /
   * up_rtc_getdatetime() / up_rtc_settime() from these ops, which is what
   * seeds CLOCK_REALTIME and what clock_settime() lands on.
   */

  up_rtc_set_lowerhalf(&g_bk7258_rtc.lower, true);

  /* Then expose it as /dev/rtc0 for the ioctl-level users. */

  ret = rtc_initialize(0, &g_bk7258_rtc.lower);
  return ret < 0 ? ret : OK;
}

/****************************************************************************
 * Probe hook (see bk7258_probe.h)
 ****************************************************************************/

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/timers/rtc.h>

#include "bk7258_probe.h"

/* Read /dev/rtc0 and CLOCK_REALTIME, move the clock to a known date through
 * the ioctl, then read both again: that is the whole path -- lower half ->
 * rtc.c upper half -> arch_rtc bridge -> clock_settime.
 */

void bk7258_rtc_probe(void)
{
  struct rtc_time rt;
  struct timespec ts;
  int fd = open("/dev/rtc0", O_RDWR);

  if (fd < 0)
    {
      printf("rtc: open(/dev/rtc0) failed: %d\n", errno);
      return;
    }

  if (ioctl(fd, RTC_RD_TIME, &rt) < 0)
    {
      printf("rtc: RTC_RD_TIME failed: %d\n", errno);
      close(fd);
      return;
    }

  printf("rtc: rtc0=%04d-%02d-%02d %02d:%02d:%02d\n",
         rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
         rt.tm_hour, rt.tm_min, rt.tm_sec);

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
      printf("rtc: CLOCK_REALTIME=%ld s\n", (long)ts.tv_sec);
    }

  /* Move it to a fixed instant and read both faces back. */

  rt.tm_year = 2026 - 1900;
  rt.tm_mon  = 1 - 1;
  rt.tm_mday = 1;
  rt.tm_hour = 0;
  rt.tm_min  = 0;
  rt.tm_sec  = 0;

  printf("rtc: RTC_SET_TIME(2026-01-01 00:00:00) -> %d\n",
         ioctl(fd, RTC_SET_TIME, &rt));

  if (ioctl(fd, RTC_RD_TIME, &rt) == 0)
    {
      printf("rtc: rtc0 after set = %04d-%02d-%02d %02d:%02d:%02d\n",
             rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
             rt.tm_hour, rt.tm_min, rt.tm_sec);
    }

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
      printf("rtc: CLOCK_REALTIME after set = %ld s\n", (long)ts.tv_sec);
    }

  /* Alarm face: ask the framework for +5 s through /dev/rtc0 and wait for the
   * signal.  That exercises lower half -> the driver's alarm engine -> its tick
   * ISR -> the framework's notification.
   */

  {
    struct rtc_setalarm_s ai;
    struct timespec to;
    sigset_t set;
    time_t when;
    int sig;

    memset(&ai, 0, sizeof(ai));
    memset(&to, 0, sizeof(to));

    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, NULL);

    when = s_epoch_s + (time_t)(bk_aon_rtc_get_us() / 1000000u) + 5;
    gmtime_r(&when, (struct tm *)&ai.time);

    ai.id                 = 0;
    ai.pid                = getpid();
    ai.event.sigev_notify = SIGEV_SIGNAL;
    ai.event.sigev_signo  = SIGALRM;

    printf("rtc: RTC_SET_ALARM(+5s) -> %d\n", ioctl(fd, RTC_SET_ALARM, &ai));

    to.tv_sec = 8;
    sig       = sigtimedwait(&set, NULL, &to);

    printf("rtc: alarm fired=%d (%s)\n", sig, sig == SIGALRM ? "ok" : "timeout");

    ioctl(fd, RTC_CANCEL_ALARM, 0);
  }

  close(fd);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

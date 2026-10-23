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
 * Only the time-of-day part of the framework is implemented: rdtime, settime
 * and havesettime.  Alarms stay on the Armino side for now -- pm.c /
 * low_pwr_* register their wakeups through bk_alarm_register(), and moving
 * those onto the framework's alarm API belongs to the PM batch, not here.
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

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Seconds added to the raw counter to make up the requested wall clock. */

static time_t s_epoch_s;

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

  return OK;
}

static bool bk7258_rtc_havesettime(struct rtc_lowerhalf_s *lower)
{
  return true;
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rtc_ops_s g_bk7258_rtc_ops =
{
  .rdtime      = bk7258_rtc_rdtime,
  .settime     = bk7258_rtc_settime,
  .havesettime = bk7258_rtc_havesettime,
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

  close(fd);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

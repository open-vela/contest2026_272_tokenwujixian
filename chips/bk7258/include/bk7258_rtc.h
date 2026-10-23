/****************************************************************************
 * chips/bk7258/include/bk7258_rtc.h
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

#ifndef __CHIPS_BK7258_INCLUDE_BK7258_RTC_H
#define __CHIPS_BK7258_INCLUDE_BK7258_RTC_H

#include <nuttx/config.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#ifdef __cplusplus
extern "C"
{
#endif

/* Register /dev/rtc0 over the AON RTC counter.  Requires CONFIG_RTC. */

int bk7258_rtc_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* !CONFIG_BK7258_COMPONENT_AP */

#endif /* __CHIPS_BK7258_INCLUDE_BK7258_RTC_H */

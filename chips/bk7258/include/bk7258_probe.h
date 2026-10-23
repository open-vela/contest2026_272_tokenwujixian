/****************************************************************************
 * chips/bk7258/include/bk7258_probe.h
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

#ifndef __CHIPS_BK7258_INCLUDE_BK7258_PROBE_H
#define __CHIPS_BK7258_INCLUDE_BK7258_PROBE_H

#include <nuttx/config.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#ifdef __cplusplus
extern "C"
{
#endif

/* Exercise the seven UART driver symbols that remain referenced in the image
 * and print what each returns.  Evidence for the replacement, not a feature.
 */

void bk7258_uart_probe(void);

/* Exercise the exception bookkeeping (lock, ownership read, repair, the direct
 * UART writer) without faulting, and print what each step returned.  Evidence
 * for the trap_base replacement, not a feature.
 */

void bk7258_trap_probe(void);

/* Walk the device-model table: handle encoding, the magic-word check, a
 * register/dispatch/close/unregister round trip, and the failure code an
 * unregistered slot returns.
 */

void bk7258_drv_model_probe(void);

/* Read /dev/rtc0 and CLOCK_REALTIME, move the clock through the RTC ioctl and
 * read both faces back.  Evidence for the AON RTC + framework landing.
 */

void bk7258_rtc_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* !CONFIG_BK7258_COMPONENT_AP */

#endif /* __CHIPS_BK7258_INCLUDE_BK7258_PROBE_H */

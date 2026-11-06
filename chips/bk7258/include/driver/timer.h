/****************************************************************************
 * chips/bk7258/include/driver/timer.h
 *
 * This file replaces cp/include/driver/timer.h on the include path once the
 * preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
 * shows the two declare the same contract.  The implementation is
 * chips/bk7258/bk7258_timer.c.
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_TIMER_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_TIMER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* The authority header pulls common/bk_include.h; callers rely on that
 * transitive surface, so it stays.
 */

#include <common/bk_include.h>
#include <driver/timer_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

bk_err_t bk_timer_driver_init(void);
bk_err_t bk_timer_driver_deinit(void);
bk_err_t bk_timer_start(timer_id_t timer_id, uint32_t time_ms,
                        timer_isr_t callback);
bk_err_t bk_timer_start_without_callback(timer_id_t timer_id,
                                         uint32_t time_ms);
bk_err_t bk_timer_stop(timer_id_t timer_id);
uint32_t bk_timer_get_cnt(timer_id_t timer_id);
bk_err_t bk_timer_enable(timer_id_t timer_id);
bk_err_t bk_timer_disable(timer_id_t timer_id);
uint32_t bk_timer_get_period(timer_id_t timer_id);
uint32_t bk_timer_get_enable_status(void);
bool bk_timer_is_interrupt_triggered(timer_id_t timer_id);
uint64_t bk_timer_get_time(timer_id_t timer_id, uint32_t div,
                           uint32_t last_count,
                           timer_value_unit_t unit_type);
bk_err_t bk_timer_delay_with_callback(timer_id_t timer_id, uint64_t time_us,
                                      timer_isr_t callback);
bk_err_t bk_timer_cancel(timer_id_t timer_id);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_TIMER_H */

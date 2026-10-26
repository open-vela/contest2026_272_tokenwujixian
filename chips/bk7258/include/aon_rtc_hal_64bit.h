/****************************************************************************
 * chips/bk7258/include/aon_rtc_hal_64bit.h
 *
 * This file replaces middleware/soc/common/hal/include/aon_rtc_hal_64bit.h
 * on the include path once the preprocessing comparison
 * (my_docs/bk7258-wifi/tools/header_equiv.py) shows the two declare the
 * same contract.  The implementation is chips/bk7258/bk7258_aon_rtc_hal.c;
 * aon_rtc_ll.h stays the authority's register layer underneath.
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

#ifndef __CHIPS_BK7258_INCLUDE_AON_RTC_HAL_64BIT_H
#define __CHIPS_BK7258_INCLUDE_AON_RTC_HAL_64BIT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Same four includes as the authority header: callers reach aon_rtc_ll_*
 * and aon_rtc_hw_t through this one.
 */

#include "hal_config.h"
#include "aon_rtc_hw.h"
#include "aon_rtc_ll.h"
#include <driver/hal/hal_aon_rtc_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AON_RTC_HAL_UNIT_NUM (SOC_AON_RTC_UNIT_NUM)

#if CFG_HAL_DEBUG_AON_RTC
void aon_rtc_struct_dump(void);
#else
#define aon_rtc_struct_dump()
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct
{
  aon_rtc_hw_t  *hw;  /* register base, not allocated */
  aon_rtc_unit_t id;
} aon_rtc_hal_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void aon_rtc_hal_enable(aon_rtc_hal_t *hal);
void aon_rtc_hal_disable(aon_rtc_hal_t *hal);
bool aon_rtc_hal_is_enable(aon_rtc_hal_t *hal);

void aon_rtc_hal_stop_counter(aon_rtc_hal_t *hal);
void aon_rtc_hal_start_counter(aon_rtc_hal_t *hal);
bool aon_rtc_hal_is_counter_stop(aon_rtc_hal_t *hal);

void aon_rtc_hal_reset_counter(aon_rtc_hal_t *hal);
void aon_rtc_hal_clear_reset_counter(aon_rtc_hal_t *hal);
bool aon_rtc_hal_is_counter_reset(aon_rtc_hal_t *hal);

void aon_rtc_hal_set_tick_val(aon_rtc_hal_t *hal, uint64_t val);
uint64_t aon_rtc_hal_get_tick_val(aon_rtc_hal_t *hal);
void aon_rtc_hal_enable_tick_int(aon_rtc_hal_t *hal);
void aon_rtc_hal_disable_tick_int(aon_rtc_hal_t *hal);
bool aon_rtc_hal_is_tick_int_enable(aon_rtc_hal_t *hal);
bool aon_rtc_hal_get_tick_int_status(aon_rtc_hal_t *hal);
void aon_rtc_hal_clear_tick_int_status(aon_rtc_hal_t *hal);

void aon_rtc_hal_set_upper_val(aon_rtc_hal_t *hal, uint64_t up_val);
uint64_t aon_rtc_hal_get_upper_val(aon_rtc_hal_t *hal);
void aon_rtc_hal_enable_upper_int(aon_rtc_hal_t *hal);
void aon_rtc_hal_disable_upper_int(aon_rtc_hal_t *hal);
bool aon_rtc_hal_is_upper_int_enable(aon_rtc_hal_t *hal);
bool aon_rtc_hal_get_upper_int_status(aon_rtc_hal_t *hal);
void aon_rtc_hal_clear_upper_int_status(aon_rtc_hal_t *hal);

uint64_t aon_rtc_hal_get_counter_val(aon_rtc_hal_t *hal);
uint64_t aon_rtc_hal_get_upper_val_lpo(aon_rtc_hal_t *hal);
uint64_t aon_rtc_hal_get_tick_val_lpo(aon_rtc_hal_t *hal);

bk_err_t aon_rtc_hal_init(aon_rtc_hal_t *hal);
bk_err_t aon_rtc_hal_deinit(aon_rtc_hal_t *hal);

/* The authority declares this one without (void); kept, since the empty
 * parameter list is part of the declared contract.
 */

bk_err_t aon_rtc_hal_tick_init();
bk_err_t aon_rtc_hal_open_rtc_wakeup(uint32_t period);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_AON_RTC_HAL_64BIT_H */

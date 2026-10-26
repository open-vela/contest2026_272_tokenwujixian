/****************************************************************************
 * chips/bk7258/include/aon_wdt_hal.h
 *
 * This file replaces middleware/soc/common/hal/include/aon_wdt_hal.h on the
 * include path once the preprocessing comparison (my_docs/bk7258-wifi/
 * tools/header_equiv.py) shows the two declare the same contract.  The
 * implementation is chips/bk7258/bk7258_wdt.c; aon_wdt_ll.h stays the
 * authority's register layer underneath.
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

#ifndef __CHIPS_BK7258_INCLUDE_AON_WDT_HAL_H
#define __CHIPS_BK7258_INCLUDE_AON_WDT_HAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "aon_wdt_ll.h"

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if CFG_HAL_DEBUG_WDT
void aon_wdt_struct_dump(void);
#else
#define aon_wdt_struct_dump()
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

uint32_t aon_wdt_hal_get_period(void);
bk_err_t aon_wdt_hal_set_period(uint32_t timeout);
bk_err_t aon_wdt_hal_stop_aon_wdt(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_AON_WDT_HAL_H */

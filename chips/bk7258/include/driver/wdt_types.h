/****************************************************************************
 * chips/bk7258/include/driver/wdt_types.h
 *
 * This file replaces cp/include/driver/wdt_types.h on the include path once
 * the preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
 * shows the two declare the same contract.  The implementation is
 * chips/bk7258/bk7258_wdt.c.
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_WDT_TYPES_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_WDT_TYPES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <common/bk_include.h>
#include <driver/hal/hal_wdt_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK_ERR_WDT_DRIVER_NOT_INIT (BK_ERR_WDT_BASE - 1) /* driver not init */
#define BK_ERR_WDT_NOT_INIT        (BK_ERR_WDT_BASE - 2) /* WDT not init */
#define BK_ERR_WDT_INVALID_PERIOD  (BK_ERR_WDT_BASE - 3) /* invalid period */
#define BK_ERR_WDT_DEBUG_DISABLED  (BK_ERR_WDT_BASE - 4) /* cmdline off */

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_WDT_TYPES_H */

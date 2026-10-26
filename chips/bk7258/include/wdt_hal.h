/****************************************************************************
 * chips/bk7258/include/wdt_hal.h
 *
 * This file replaces middleware/soc/common/hal/include/wdt_hal.h on the
 * include path once the preprocessing comparison (my_docs/bk7258-wifi/
 * tools/header_equiv.py) shows the two declare the same contract.  The
 * implementation is chips/bk7258/bk7258_wdt.c; wdt_ll.h stays the
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

#ifndef __CHIPS_BK7258_INCLUDE_WDT_HAL_H
#define __CHIPS_BK7258_INCLUDE_WDT_HAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Same four includes as the authority header: callers reach wdt_ll_* and
 * wdt_hw_t through this one.
 */

#include "hal_config.h"
#include "wdt_hw.h"
#include "wdt_ll.h"
#include <driver/hal/hal_wdt_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  AON_WDT_ID = 0,
  NMI_WDT_ID,
} wdt_id_t;

typedef struct
{
  wdt_hw_t  *hw;
  wdt_unit_t id;
} wdt_hal_t;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define wdt_hal_reset_config_to_default(hal) \
  wdt_ll_reset_config_to_default((hal)->hw)

#if CFG_HAL_DEBUG_WDT
void wdt_struct_dump(void);
#else
#define wdt_struct_dump()
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

bk_err_t wdt_hal_init(wdt_hal_t *hal);
bk_err_t wdt_hal_init_wdt(wdt_hal_t *hal, uint32_t timeout);
void wdt_hal_close(void);
void wdt_hal_force_feed(void);
void wdt_hal_force_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_WDT_HAL_H */

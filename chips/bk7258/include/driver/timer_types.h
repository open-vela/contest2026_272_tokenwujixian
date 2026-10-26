/****************************************************************************
 * chips/bk7258/include/driver/timer_types.h
 *
 * This file replaces cp/include/driver/timer_types.h on the include path
 * once the preprocessing comparison (my_docs/bk7258-wifi/tools/
 * header_equiv.py) shows the two declare the same contract.  The
 * declarations are here; the authority header is no longer searched first.
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_TIMER_TYPES_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_TIMER_TYPES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* timer_id_t, timer_unit_t and timer_value_unit_t live here, as in the
 * authority header.
 */

#include <driver/hal/hal_timer_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK_ERR_TIMER_ID              (BK_ERR_TIMER_BASE - 1) /* invalid id */
#define BK_ERR_TIMER_NOT_INIT        (BK_ERR_TIMER_BASE - 2) /* not init */
#define BK_ERR_TIMER_IS_RUNNING      (BK_ERR_TIMER_BASE - 3) /* running */
#define BK_ERR_TIMER_ID_ON_DEFCONFIG (BK_ERR_TIMER_BASE - 4) /* id off */

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*timer_isr_t)(timer_id_t timer_id);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_TIMER_TYPES_H */

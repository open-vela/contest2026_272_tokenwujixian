/****************************************************************************
 * chips/bk7258/include/driver/gpio_types.h
 *
 * This file replaces cp/include/driver/gpio_types.h on the include path once
 * the preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
 * shows the two declare the same contract.  The implementation is
 * chips/bk7258/bk7258_gpio.c.
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_GPIO_TYPES_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_GPIO_TYPES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <common/bk_include.h>
#include <driver/hal/hal_gpio_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK_ERR_GPIO_CHAN_ID                 (BK_ERR_GPIO_BASE - 1)
#define BK_ERR_GPIO_INVALID_MODE            (BK_ERR_GPIO_BASE - 2)
#define BK_ERR_GPIO_NOT_INPUT_MODE          (BK_ERR_GPIO_BASE - 3)
#define BK_ERR_GPIO_SET_INVALID_FUNC_MODE   (BK_ERR_GPIO_BASE - 4)
#define BK_ERR_GPIO_INVALID_INT_TYPE        (BK_ERR_GPIO_BASE - 5)
#define BK_ERR_GPIOS_MAP_NONE               (BK_ERR_GPIO_BASE - 6)
#define BK_ERR_GPIO_NOT_OUTPUT_MODE         (BK_ERR_GPIO_BASE - 7)
#define BK_ERR_GPIO_BITS_NUM                (BK_ERR_GPIO_BASE - 8)
#define BK_ERR_GPIO_INTERNAL_USED           (BK_ERR_GPIO_BASE - 9)
#define BK_ERR_GPIO_MAP_PWMS_CHAN           (BK_ERR_GPIO_BASE - 10)
#define BK_ERR_GPIO_INVALID_ID              (BK_ERR_GPIO_BASE - 11)
#define BK_ERR_GPIO_WAKESOURCE_OVER_MAX_CNT (BK_ERR_GPIO_BASE - 12)
#define BK_ERR_ANA_GPIO_TYPE_NOT_SUPPORT    (BK_ERR_GPIO_BASE - 13)
#define BK_ERR_GPIO_INVALID_OPERATE         (BK_ERR_GPIO_BASE - 14)
#define BK_ERR_GPIO_CONFIG_MISMATCH         (BK_ERR_GPIO_BASE - 15)

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*gpio_isr_t)(gpio_id_t gpio_id);

typedef struct
{
  uint32_t        id;
  gpio_int_type_t int_type;
  gpio_isr_t      isr;
  uint32_t        valid;
} gpio_wakeup_config_t;

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_GPIO_TYPES_H */

/****************************************************************************
 * chips/bk7258/include/gpio_hal.h
 *
 * This file replaces middleware/soc/common/hal/include/gpio_hal.h on the
 * include path once the preprocessing comparison (my_docs/bk7258-wifi/
 * tools/header_equiv.py) shows the two declare the same contract.  The
 * implementation is chips/bk7258/bk7258_gpio_hal.c; gpio_ll.h stays the
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

#ifndef __CHIPS_BK7258_INCLUDE_GPIO_HAL_H
#define __CHIPS_BK7258_INCLUDE_GPIO_HAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Same four includes as the authority header: callers (bk7258_gpio.c, the
 * flash bypass, the UART driver) reach gpio_ll_* and gpio_hw_t through it.
 */

#include "hal_config.h"
#include "hal_port.h"
#include "gpio_ll.h"
#include "bk_icu.h"

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct
{
  gpio_hw_t *hw;
  gpio_id_t  gpio_id;
} gpio_hal_t;

typedef struct
{
  gpio_int_type_t gpio_int_type;
} gpio_hal_inttype_config_t;

typedef struct
{
  gpio_id_t  id;
  gpio_dev_t dev[GPIO_PERI_FUNC_NUM];
  bool       is_available;
} gpio_map_t;

#if CONFIG_GPIO_WAKEUP_SUPPORT
typedef enum
{
  GPIO_WAKEUP_UP_EVENT = 0,
  GPIO_CANCEL_WAKEUP_EVENT,
  GPIO_KEEP_STATUS_EVENT,
  GPIO_CANCEL_STATUS_EVENT
} gpio_lower_power_event_t;

typedef struct
{
  gpio_id_t       id;
  gpio_int_type_t int_type;
} gpio_wakeup_t;

typedef struct
{
  gpio_id_t                gpio_id;
  gpio_lower_power_event_t event;
} gpio_header_info_t;

typedef struct
{
  gpio_header_info_t header;
  union
  {
    gpio_int_type_t int_type;
    gpio_config_t   config;
  } data;
} gpio_lowerpower_t;
#endif

typedef struct
{
  gpio_id_t gpio_id;
  uint32_t  ldo_state;
} gpio_ctrl_ldo_t;

#if CONFIG_GPIO_DEFAULT_SET_SUPPORT
typedef struct
{
  uint32_t gpio_id:                     6;
  uint32_t second_func_en:              1;
  uint32_t second_func_dev:            12;
  uint32_t io_mode:                     2;
  uint32_t pull_mode:                   2;
  uint32_t int_en:                      1;
  uint32_t int_type:                    2;
  uint32_t low_power_io_ctrl:           2;
  uint32_t driver_capacity:             2;
  uint32_t gpio_init:                   1;
  uint32_t gpio_time_sharing_multiplex: 1;
} gpio_default_map_t;

bk_err_t gpio_hal_default_map_init(gpio_hal_t *hal);
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

bk_err_t gpio_hal_init(gpio_hal_t *hal);
bk_err_t gpio_hal_set_value(gpio_hal_t *hal, gpio_id_t id, uint32_t v);
uint32_t gpio_hal_get_value(gpio_hal_t *hal, gpio_id_t id);
bk_err_t gpio_hal_output_enable(gpio_hal_t *hal, gpio_id_t gpio_id,
                                uint32 enable);
bk_err_t gpio_hal_input_enable(gpio_hal_t *hal, gpio_id_t gpio_id,
                               uint32 enable);
bk_err_t gpio_hal_pull_up_enable(gpio_hal_t *hal, gpio_id_t gpio_id,
                                 uint32 enable);
bk_err_t gpio_hal_pull_enable(gpio_hal_t *hal, gpio_id_t gpio_id,
                              uint32 enable);

/* "sencond" is the authority's spelling; it is the exported name. */

bk_err_t gpio_hal_sencond_function_enable(gpio_hal_t *hal, gpio_id_t gpio_id,
                                          uint32 enable);
bk_err_t gpio_hal_monitor_input_enable(gpio_hal_t *hal, gpio_id_t gpio_id,
                                       uint32 enable);
bk_err_t gpio_hal_set_capacity(gpio_hal_t *hal, gpio_id_t gpio_id,
                               uint32 capacity);
bk_err_t gpio_hal_set_output_value(gpio_hal_t *hal, gpio_id_t gpio_id,
                                   uint32 output_value);
bk_err_t gpio_hal_get_output(gpio_hal_t *hal, gpio_id_t gpio_id);
bk_err_t gpio_hal_get_input(gpio_hal_t *hal, gpio_id_t gpio_id);
bk_err_t gpio_hal_set_int_type(gpio_hal_t *hal, gpio_id_t gpio_id,
                               gpio_int_type_t type);
bk_err_t gpio_hal_func_map(gpio_hal_t *hal, gpio_id_t id, gpio_dev_t dev);
bk_err_t gpio_hal_func_unmap(gpio_hal_t *hal, gpio_id_t gpio_id);
bk_err_t gpio_hal_set_config(gpio_hal_t *hal, gpio_id_t gpio_id,
                             const gpio_config_t *config);
bk_err_t gpio_hal_devs_map(gpio_hal_t *hal, uint64 gpios, gpio_dev_t *devs,
                           uint8 dev_num);
bk_err_t gpio_hal_enable_interrupt(gpio_hal_t *hal, gpio_id_t gpio_id);
bk_err_t gpio_hal_dump_map_dev_cfg(gpio_hal_t *hal);

#if CONFIG_GPIO_WAKEUP_SUPPORT
bk_err_t gpio_hal_bak_configs(uint16_t *gpio_cfg, uint32_t count);
bk_err_t gpio_hal_restore_configs(uint16_t *gpio_cfg, uint32_t count);
bk_err_t gpio_hal_bak_int_type_configs(uint32_t *gpio_int_type_cfg,
                                       uint32_t count);
bk_err_t gpio_hal_restore_int_type_configs(uint32_t *gpio_int_type_cfg,
                                           uint32_t count);
bk_err_t gpio_hal_bak_int_enable_configs(uint32_t *gpio_int_enable_cfg,
                                         uint32_t count);
bk_err_t gpio_hal_restore_int_enable_configs(uint32_t *gpio_int_enable_cfg,
                                             uint32_t count);
bk_err_t gpio_hal_switch_to_low_power_status(uint64_t skip_io);
#else
bk_err_t gpio_hal_reg_save(uint32_t *gpio_cfg);
bk_err_t gpio_hal_reg_restore(uint32_t *gpio_cfg);
bk_err_t gpio_hal_wakeup_enable(int64_t index, uint64_t type_l,
                                uint64_t type_h);

/* Empty parameter list kept as the authority declares it. */

bk_err_t gpio_hal_wakeup_interrupt_clear();
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define gpio_hal_disable_interrupt(hal, id) \
  gpio_ll_disable_interrupt((hal)->hw, id)
#define gpio_hal_get_interrupt_status(hal, status) \
  gpio_ll_get_interrupt_status((hal)->hw, status)
#define gpio_hal_clear_interrupt_status(hal,status) \
  gpio_ll_clear_interrupt_status((hal)->hw, status)
#define gpio_hal_is_interrupt_triggered(hal, id,status) \
  gpio_ll_is_interrupt_triggered((hal)->hw, id, status)
#define gpio_hal_clear_chan_interrupt_status(hal, id) \
  gpio_ll_clear_chan_interrupt_status((hal)->hw, id)

/* The no-argument fallback is the authority's (it does not match the
 * declared prototype's arity); kept so the macro text is unchanged.
 */

#if CFG_HAL_DEBUG_GPIO
void gpio_struct_dump(gpio_id_t gpio_index);
#else
#define gpio_struct_dump()
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_GPIO_HAL_H */

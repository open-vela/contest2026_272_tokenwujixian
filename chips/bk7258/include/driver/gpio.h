/****************************************************************************
 * chips/bk7258/include/driver/gpio.h
 *
 * This file replaces cp/include/driver/gpio.h on the include path once the
 * preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_GPIO_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_GPIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* The authority header pulls common/bk_include.h; callers rely on that
 * transitive surface, so it stays.
 */

#include <common/bk_include.h>
#include <driver/gpio_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GPIO_LDO_DEFINE_APP_MODULE(offset) \
  ((uint32_t)(GPIO_CTRL_LDO_MODULE_APP_BASE + (offset)))

#define GPIO_LDO_MODULE_IS_VALID(id) \
  ((id) < GPIO_CTRL_LDO_MODULE_MAX)

#define GPIO_LDO_MODULE_IS_APP_RANGE(id) \
  (((id) >= GPIO_CTRL_LDO_MODULE_APP_BASE) && \
   ((id) < GPIO_CTRL_LDO_MODULE_MAX))

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

bk_err_t bk_gpio_driver_init(void);
bk_err_t bk_gpio_driver_deinit(void);

bk_err_t bk_gpio_set_value(gpio_id_t id, uint32_t v);
uint32_t bk_gpio_get_value(gpio_id_t id);

bk_err_t bk_gpio_enable_output(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_output(gpio_id_t gpio_id);
bk_err_t bk_gpio_enable_input(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_input(gpio_id_t gpio_id);
bk_err_t bk_gpio_enable_pull(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_pull(gpio_id_t gpio_id);
bk_err_t bk_gpio_pull_up(gpio_id_t gpio_id);
bk_err_t bk_gpio_pull_down(gpio_id_t gpio_id);
bk_err_t bk_gpio_set_config(gpio_id_t gpio_id, const gpio_config_t *config);
bk_err_t bk_gpio_set_output_high(gpio_id_t gpio_id);
bk_err_t bk_gpio_set_output_low(gpio_id_t gpio_id);
bool bk_gpio_get_input(gpio_id_t gpio_id);
bool bk_gpio_set_capacity(gpio_id_t gpio_id, uint32 capacity);

bk_err_t bk_gpio_set_interrupt_type(gpio_id_t gpio_id, gpio_int_type_t type);
bk_err_t bk_gpio_enable_interrupt(gpio_id_t id);
bk_err_t bk_gpio_disable_interrupt(gpio_id_t id);
bk_err_t bk_gpio_clear_interrupt(gpio_id_t gpio_id);
bk_err_t bk_gpio_register_isr(gpio_id_t id, gpio_isr_t isr);
bk_err_t bk_gpio_unregister_isr(gpio_id_t id);

bk_err_t bk_gpio_ctrl_external_ldo(uint32_t module, gpio_id_t gpio_id,
                                   gpio_output_state_e value);

/* Empty parameter lists below are the authority's declarations, kept as
 * written: they are part of the declared contract.
 */

#if CONFIG_GPIO_DYNAMIC_WAKEUP_SUPPORT
bk_err_t bk_gpio_register_wakeup_source(gpio_id_t gpio_id,
                                        gpio_int_type_t int_type);
bk_err_t bk_gpio_unregister_wakeup_source(gpio_id_t gpio_id);
gpio_id_t bk_gpio_get_wakeup_gpio_id();
bk_err_t bk_gpio_register_lowpower_keep_status(gpio_id_t gpio_id,
                                               const gpio_config_t *config);
bk_err_t bk_gpio_unregister_lowpower_keep_status(gpio_id_t gpio_id);
#else
bk_err_t bk_gpio_reg_save(uint32_t *gpio_cfg);
bk_err_t bk_gpio_reg_restore(uint32_t *gpio_cfg);
bk_err_t bk_gpio_wakeup_enable(int64_t index, uint64_t type_l,
                               uint64_t type_h);
bk_err_t bk_gpio_wakeup_interrupt_clear();
#endif

#if CONFIG_GPIO_ANA_WAKEUP_SUPPORT
bk_err_t bk_gpio_ana_register_wakeup_source(gpio_id_t gpio_id,
                                            gpio_int_type_t int_type);
#endif

#if CONFIG_PM
bk_err_t bk_gpio_wakeup_config_set(gpio_id_t id, gpio_int_type_t type);
#endif

#if CONFIG_GPIO_RETENTION_SUPPORT
bk_err_t bk_gpio_retention_set(gpio_id_t gpio_id,
                               gpio_output_state_e gpio_output_state);
bk_err_t bk_gpio_retention_clr(gpio_id_t gpio_id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_GPIO_H */

/****************************************************************************
 * chips/bk7258/include/driver/uart.h
 *
 * This file replaces cp/include/driver/uart.h on the include path once the
 * preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
 * shows the two declare the same contract.  The implementation is
 * chips/bk7258/bk7258_uart_driver.c.
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_UART_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_UART_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* The authority header pulls common/bk_include.h; callers rely on that
 * transitive surface, so it stays.
 */

#include <common/bk_include.h>
#include <driver/uart_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

bk_err_t bk_uart_driver_init(void);
bk_err_t bk_uart_driver_deinit(void);
int bk_uart_is_in_used(uart_id_t id);
int bk_uart_is_rx_dma_enabled(uart_id_t id);

bk_err_t bk_uart_init(uart_id_t id, const uart_config_t *config);
bk_err_t bk_uart_deinit(uart_id_t id);

bk_err_t bk_uart_set_baud_rate(uart_id_t id, uint32_t baud_rate);
bk_err_t bk_uart_set_data_bits(uart_id_t id, uart_data_bits_t data_bits);
bk_err_t bk_uart_set_stop_bits(uart_id_t id, uart_stop_bits_t stop_bits);
bk_err_t bk_uart_set_parity(uart_id_t id, uart_parity_t partiy);
bk_err_t bk_uart_set_hw_flow_ctrl(uart_id_t id, uint8_t rx_threshold);
bk_err_t bk_uart_set_rx_full_threshold(uart_id_t id, uint8_t threshold);
bk_err_t bk_uart_set_tx_empty_threshold(uart_id_t id, uint8_t threshold);
bk_err_t bk_uart_set_rx_timeout(uart_id_t id,
                                uart_rx_stop_detect_time_t timeout_thresh);
bk_err_t bk_uart_disable_hw_flow_ctrl(uart_id_t id);

bk_err_t bk_uart_enable_tx_interrupt(uart_id_t id);
bk_err_t bk_uart_disable_tx_interrupt(uart_id_t id);
bk_err_t bk_uart_enable_rx_interrupt(uart_id_t id);
bk_err_t bk_uart_disable_rx_interrupt(uart_id_t id);
bk_err_t bk_uart_register_rx_isr(uart_id_t id, uart_isr_t isr, void *param);
bk_err_t bk_uart_register_tx_isr(uart_id_t id, uart_isr_t isr, void *param);

bk_err_t bk_uart_write_bytes(uart_id_t id, const void *data, uint32_t size);
int bk_uart_read_bytes(uart_id_t id, void *data, uint32_t size,
                       uint32_t timeout_ms);

bk_err_t bk_uart_disable_rx(uart_id_t id);
bk_err_t bk_uart_disable_tx(uart_id_t id);
bk_err_t bk_uart_set_enable_rx(uart_id_t id, bool enable);
bk_err_t bk_uart_set_enable_tx(uart_id_t id, bool enable);
bk_err_t bk_uart_enable_sw_fifo(uart_id_t id);
bk_err_t bk_uart_disable_sw_fifo(uart_id_t id);

uint32_t bk_uart_get_ate_detect_gpio(void);
bk_err_t bk_uart_take_rx_isr(uart_id_t id, uart_isr_t isr, void *param);
bk_err_t bk_uart_recover_rx_isr(uart_id_t id);
bool bk_uart_is_tx_over(uart_id_t id);

bk_err_t bk_uart_pm_backup(uart_id_t id);
bk_err_t bk_uart_pm_restore(uart_id_t id);
void bk_uart_wait_tx_over(uart_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_UART_H */

/*
 * chips/bk7258/wifi/hal_port/include/sys_driver.h
 *
 * NuttX reimplementation of the Armino sys_ctrl driver callbacks the vendored
 * glue registers into the Wi-Fi driver capability table. Only the interrupt
 * and modem-clock subset used by rw_task.c / bk_wifi_adapter.c is declared;
 * implementations are in hw_driver_shim.c and map onto the team chip layer.
 */

#ifndef __BK7258_WIFI_GLUE_SYS_DRIVER_H
#define __BK7258_WIFI_GLUE_SYS_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_include.h>
#include <components/log.h>
#include <modules/pm.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t sys_drv_int_disable(uint32_t param);
int32_t sys_drv_int_enable(uint32_t param);
int32_t sys_drv_int_group2_disable(uint32_t param);
int32_t sys_drv_int_group2_enable(uint32_t param);

/* Reached by the imported authority AON RTC driver's super-deep-sleep enter
 * callback (bk_rtc_ana_register_wakeup_source, CONFIG_RTC_ANA_WAKEUP_SUPPORT).
 * Implemented in aon_rtc/nuttx_port/aon_rtc_nuttx_port.c, which reproduces the
 * authority sys_pm_hal.c:1127 analog-SPI sequence. */
void sys_drv_rtc_ana_wakeup_enable(uint32_t period);

void sys_drv_sadc_int_enable(void);
void sys_drv_sadc_int_disable(void);
void sys_drv_sadc_pwr_up(void);
void sys_drv_sadc_pwr_down(void);
void sys_drv_en_tempdet(uint32_t value);

uint32_t sys_drv_modem_bus_clk_ctrl(bool clk_en);
uint32_t sys_drv_modem_clk_ctrl(bool clk_en);

uint32_t sys_drv_cali_dpll(uint32_t param);
void sys_drv_set_ana_cb_cal_manu_val(uint32_t value);
void sys_drv_set_ana_ioldo_lp(uint32_t value);
void sys_drv_set_ana_cb_cal_trig(uint32_t value);
void sys_drv_set_ana_cb_cal_manu(uint32_t value);
uint32_t sys_drv_analog_set_xtalh_ctune(uint32_t param);
uint32_t sys_drv_get_bgcalm(void);
uint32_t sys_drv_set_bgcalm(uint32_t param);
uint32_t sys_drv_get_vdd_value(void);
uint32_t sys_drv_set_vdd_value(uint32_t param);
void sys_drv_module_power_ctrl(power_module_name_t module,
                               power_module_state_t state);
void sys_drv_set_ana_reg11_apfms(uint32_t value);
void sys_drv_set_ana_reg12_dpfms(uint32_t value);
void sys_hal_enter_low_analog(void);
void sys_hal_exit_low_analog(void);

int32_t sys_drv_module_power_state_get(power_module_name_t module);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_SYS_DRIVER_H */

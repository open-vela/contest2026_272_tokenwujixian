/* NuttX port: the prologue below came from a wrapper translation unit
 * at wifi/hal_port/vendor_sources/bk_rf_adapter.c, which existed only to include this
 * file.  The build lists this file directly now.
 */
/* Wi-Fi-only translation unit: establish Armino's global build contract. */
#include <bk_prelude.h>

#include <common/bk_include.h>
#include <os/str.h>
#include <os/mem.h>
#include <os/os.h>
#include <components/log.h>
#include <stddef.h>
#include <stdint.h>
#include "bk_rf_adapter.h"
#include "adc_driver.h"
#include "bk_phy_internal.h"
#include "sys_driver.h"
#include "sys_ll.h"
#include <modules/pm.h>
#include "bk_wifi.h"

#ifdef CONFIG_FREERTOS_SMP
#include "spinlock.h"
#endif // CONFIG_FREERTOS_SMP

uint32_t sys_drv_modem_bus_clk_ctrl_ptr(bool clk_en)
{
	return sys_drv_modem_bus_clk_ctrl(clk_en);
}

uint32_t sys_drv_modem_clk_ctrl_ptr(bool clk_en)
{
	return sys_drv_modem_clk_ctrl(clk_en);
}

void phy_exit_dsss_only_ptr(void)
{
#if (CONFIG_SOC_BK7236XX || CONFIG_SOC_BK7239XX || CONFIG_SOC_BK7286XX)
	phy_exit_dsss_only();
#else
#endif

}

void phy_enter_dsss_only_ptr(void)
{
#if (CONFIG_SOC_BK7236XX || CONFIG_SOC_BK7239XX || CONFIG_SOC_BK7286XX)
	phy_enter_dsss_only();
#else
#endif
}

#ifdef CONFIG_FREERTOS_SMP
static volatile spinlock_t rf_spin_lock = SPIN_LOCK_INIT;
#endif // CONFIG_FREERTOS_SMP
static uint32_t rtos_disable_int_ptr(void)
{
	uint32_t int_level = rtos_disable_int();
	#ifdef CONFIG_FREERTOS_SMP
	spin_lock(&rf_spin_lock);
	#endif // CONFIG_FREERTOS_SMP
	return int_level;
}

static void rtos_enable_int_ptr(uint32_t int_level)
{
	#ifdef CONFIG_FREERTOS_SMP
	spin_unlock(&rf_spin_lock);
	#endif // CONFIG_FREERTOS_SMP
	rtos_enable_int(int_level);
}

void sys_drv_module_power_ctrl_ptr(unsigned int module, uint32_t power_state)
{
	sys_drv_module_power_ctrl(module,power_state);
}

void sys_drv_set_ana_reg11_apfms_ptr(uint32_t value)
{
	sys_drv_set_ana_reg11_apfms(value);
}

void sys_drv_set_ana_reg12_dpfms_ptr(uint32_t value)
{
	sys_drv_set_ana_reg12_dpfms(value);
}

bk_err_t bk_pm_module_vote_power_ctrl_ptr(unsigned int module, uint32_t power_state)
{
	return bk_pm_module_vote_power_ctrl((pm_power_module_name_e)module, (pm_power_module_state_e)power_state);
}

void sys_hal_low_analog_set(uint32_t en)
{
    if(en)
    {
        sys_hal_enter_low_analog();
    }else
    {
        sys_hal_exit_low_analog();
    }
}

const rf_control_funcs_t g_rf_control_funcs = {
    ._sys_drv_modem_bus_clk_ctrl  = sys_drv_modem_bus_clk_ctrl_ptr,
    ._sys_drv_modem_clk_ctrl  = sys_drv_modem_clk_ctrl_ptr,
    ._phy_exit_dsss_only = phy_exit_dsss_only_ptr,
    ._phy_enter_dsss_only = phy_enter_dsss_only_ptr,
    ._rtos_disable_int = rtos_disable_int_ptr,
    ._rtos_enable_int = rtos_enable_int_ptr,
    ._rwnx_cal_mac_sleep_rc_recover = rwnx_cal_mac_sleep_rc_recover,
    ._sys_drv_module_power_ctrl = sys_drv_module_power_ctrl_ptr,
    ._sys_drv_set_ana_reg11_apfms = sys_drv_set_ana_reg11_apfms_ptr,
    ._sys_drv_set_ana_reg12_dpfms = sys_drv_set_ana_reg12_dpfms_ptr,
    ._bk_pm_module_vote_power_ctrl = bk_pm_module_vote_power_ctrl_ptr,
    ._sys_hal_low_analog_set = sys_hal_low_analog_set,
};

const rf_variable_t g_rf_variable = {
    ._pm_power_module_state_off = PM_POWER_MODULE_STATE_OFF,
    ._pm_power_module_state_on = PM_POWER_MODULE_STATE_ON,
    ._pm_power_module_name_phy = PM_POWER_MODULE_NAME_PHY,
    ._pm_power_module_name_rf = PM_POWER_SUB_MODULE_NAME_PHY_RF,
    ._pm_power_module_name_mac = PM_POWER_MODULE_NAME_WIFIP_MAC,
    ._pm_power_module_name_ofdm = PM_POWER_MODULE_NAME_OFDM,
};

/* rf_adapter_init() in libbk_phy.a stores the active table here.  Declare it
 * explicitly so this integration validates the exact table later consumed by
 * the prebuilt RF calibration routine, rather than only the C initializer.
 */

extern const rf_control_funcs_t *g_rf_funcs_t;

int bk_rf_adapter_init(void)
{
    const rf_control_funcs_t *active_funcs;
    const uintptr_t *slots;
    size_t i;

    rf_adapter_init(&g_rf_control_funcs, &g_rf_variable);
    rf_cntrl_init();

    active_funcs = g_rf_funcs_t;
    if (active_funcs != &g_rf_control_funcs)
      {
        BK_LOGE("bk_rf", "RF callback table mismatch: active=%p expected=%p\n",
                active_funcs, &g_rf_control_funcs);
        return BK_FAIL;
      }

    slots = (const uintptr_t *)active_funcs;
    for (i = 0; i < sizeof(*active_funcs) / sizeof(*slots); i++)
      {
        if ((slots[i] & 1u) == 0)
          {
            BK_LOGE("bk_rf", "RF callback slot %u is non-Thumb: 0x%08x\n",
                    (unsigned int)i, (unsigned int)slots[i]);
            return BK_FAIL;
          }
      }

    return BK_OK;
}

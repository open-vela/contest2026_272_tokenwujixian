/*
 * chips/bk7258/wifi/hal_port/analog_shim.c
 *
 * Analog / calibration surface the vendored bk_phy_adapter.c registers into the
 * PHY capability table: crystal fine-tune, DPLL calibration, bandgap trim, core
 * and LDO voltage selects, the ADC divider, temperature and voltage monitoring.
 *
 * ==== Why none of this is a no-op ====
 *
 * Upstream reaches the analog register file through a latched write sequence
 * (sys_set_ana_reg_bit(), see the note in hal_port/include/sys_ll.h), not a single
 * store, and that sequencing layer is not ported for BK7258. Most of these
 * functions return void, so they have no way to tell libbk_phy.a that nothing
 * happened.
 *
 * A silent no-op would therefore report success by omission: the PHY would
 * proceed believing its analog front end was configured -- core voltage set,
 * bandgap trimmed, crystal pulled -- while no register was touched. On an RF
 * part that does not fail loudly; it transmits out of spec.
 *
 * So every unported entry point here logs an error naming itself, once per call
 * site per boot, and touches no hardware. Deliberately:
 *   - not silent, so bring-up sees it in the log rather than as mysterious RF
 *     behaviour;
 *   - not a panic/assert, so bring-up can still reach a state where the log is
 *     readable -- an early abort inside PHY init would hide everything after it;
 *   - rate-limited, because some of these are called from calibration loops and
 *     an unthrottled log would drown the console it is meant to inform.
 *
 * Functions that DO have an error channel (bk_err_t / int) also return failure.
 *
 * Everything in this file is a milestone-4 (RF bring-up) item, not a leftover:
 * see ABI_AUDIT.md for the blocking list.
 */

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_typedef.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <arch/chip/bk7258_sysctrl.h>

#include "sys_ll.h"
#include "sys_driver.h"
#include "drv_model_pub.h"
#include "hal_port_sys_all.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define ANALOG_TAG "bk_analog"
#define ANA_FIELD_MASK(_width, _shift) \
  (((UINT32_C(1) << (_width)) - UINT32_C(1)) << (_shift))

/* One complaint per distinct entry point per boot. The vendored PHY calls some
 * of these from calibration loops; without this the log is unusable.
 */

#define ANALOG_UNPORTED(_name)                                             \
  do                                                                       \
    {                                                                      \
      static bool _warned;                                                 \
      if (!_warned)                                                        \
        {                                                                  \
          _warned = true;                                                  \
          BK_LOGE(ANALOG_TAG,                                              \
                  "%s: BK7258 analog register access not ported; "         \
                  "RF calibration is NOT applied\n", (_name));             \
        }                                                                  \
    }                                                                      \
  while (0)

/****************************************************************************
 * sys_ll analog register accessors
 *
 * The BK7258 chip layer supplies the matching latched transaction. These are
 * field setters, not whole-register writes: each update preserves unrelated
 * bits and serializes read-modify-write with the SPI completion poll.
 ****************************************************************************/

static void bk7258_analog_set_field(const char *name, unsigned int reg,
                                    uint32_t mask, unsigned int shift,
                                    uint32_t value)
{
  int ret = bk7258_analog_update_bits(reg, mask, value << shift);

  if (ret < 0)
    {
      BK_LOGE(ANALOG_TAG, "%s: analog REG%u update failed=%d\n",
              name, reg, ret);
    }
}

/****************************************************************************
 * sys_drv analog / calibration -- UNPORTED
 ****************************************************************************/

uint32_t sys_drv_analog_set_xtalh_ctune(uint32_t param)
{
  int ret;

  /* BK7258 SDK sys_hal_set_xtalh_ctune() selects ANA_REG2[7:0]. */
  ret = bk7258_analog_update_bits(2, UINT32_C(0xff), param);
  if (ret < 0)
    {
      BK_LOGE(ANALOG_TAG, "%s: ANA2 xtalh_ctune update failed=%d\n",
              __func__, ret);
      return (uint32_t)BK_FAIL;
    }

  return BK_OK;
}

uint32_t sys_drv_cali_dpll(uint32_t param)
{
  /* HAL-alignment: call the verbatim port of the authoritative
   * sys_wifi_driver.c sequence (hp_sys_drv_cali_dpll) instead of the
   * hand-written approximation in bk7258_sysctrl.c. */
  extern uint32_t hp_sys_drv_cali_dpll(uint32_t param);

  return hp_sys_drv_cali_dpll(param);
}

/* These four were log-only stubs returning 0 / BK_FAIL (2026-09-09).  That
 * was wrong in a way the log made look handled: bk_phy_adapter.c:651-654
 * binds all four into the PHY function table with no CONFIG_SOC_BK7256XX
 * guard (unlike ._bk_efuse_read_byte, which that guard does exclude on
 * BK7258), so libbk_phy.a really calls them -- the bandgap trim and the
 * core-voltage read/write silently did nothing.  The claim in the old
 * comment, that the latched analog-register layer was not ported, no longer
 * holds: the generated authority LL header is imported verbatim and is
 * byte-identical to the reference tree, so all four upstream bodies port
 * straight across in hal_port/.  Forwarded the same way sys_drv_cali_dpll
 * above already forwards. */

uint32_t sys_drv_get_bgcalm(void)
{
  extern uint32_t hp_sys_drv_get_bgcalm(void);

  return hp_sys_drv_get_bgcalm();
}

uint32_t sys_drv_set_bgcalm(uint32_t param)
{
  extern uint32_t hp_sys_drv_set_bgcalm(uint32_t param);

  return hp_sys_drv_set_bgcalm(param);
}

uint32_t sys_drv_get_vdd_value(void)
{
  extern uint32_t hp_sys_drv_get_vdd_value(void);

  return hp_sys_drv_get_vdd_value();
}

uint32_t sys_drv_set_vdd_value(uint32_t param)
{
  extern uint32_t hp_sys_drv_set_vdd_value(uint32_t param);

  return hp_sys_drv_set_vdd_value(param);
}

void sys_drv_set_ana_cb_cal_manu(uint32_t value)
{
  bk7258_analog_set_field(__func__, 5, UINT32_C(1) << 23, 23, value);
}

void sys_drv_set_ana_cb_cal_trig(uint32_t value)
{
  bk7258_analog_set_field(__func__, 5, UINT32_C(1) << 22, 22, value);
}

void sys_drv_set_ana_cb_cal_manu_val(uint32_t value)
{
  bk7258_analog_set_field(__func__, 5, ANA_FIELD_MASK(5, 27), 27, value);
}

void sys_drv_set_ana_ioldo_lp(uint32_t value)
{
  /* Authoritative BK7258 sys_hal_set_ioldo_lp: the IO-LDO low-power flag
   * maps onto the ANA_REG8 ioldo_lp bit. */
  sys_ll_set_ana_reg8_ioldo_lp(!!value);
}

void sys_drv_set_ana_reg11_apfms(uint32_t value)
{
  bk7258_analog_set_field(__func__, 11, ANA_FIELD_MASK(5, 5), 5, value);
}

void sys_drv_set_ana_reg12_dpfms(uint32_t value)
{
  bk7258_analog_set_field(__func__, 12, ANA_FIELD_MASK(5, 8), 8, value);
}

void sys_drv_module_power_ctrl(power_module_name_t module,
                               power_module_state_t power_state)
{
  bool enable;

  /* This is the raw system-driver callback in Armino's RF function table;
   * its PM-vote callback is a separate table slot.  Do not fold the two
   * layers together: a PHY library raw transition must not change PM's
   * PHY_WIFI reference/initial-calibration state machine.
   *
   * Armino's BK7258 sys_drv_module_power_ctrl() enters a critical section
   * then dispatches to sys_hal_module_power_ctrl().  The three Wi-Fi-domain
   * cases below are the locally supported equivalent gates; each gate does
   * the register RMW and readback under the same critical-section contract.
   */

  if (power_state == POWER_MODULE_STATE_ON)
    {
      enable = true;
    }
  else if (power_state == POWER_MODULE_STATE_OFF)
    {
      enable = false;
    }
  else
    {
      ANALOG_UNPORTED("sys_drv_module_power_ctrl invalid state");
      return;
    }

  switch (module)
    {
      case POWER_MODULE_NAME_WIFIP_MAC:
        (void)bk7258_mac_power(enable);
        break;

      case POWER_MODULE_NAME_WIFI_PHY:
        (void)bk7258_phy_power(enable);
        break;

      case POWER_MODULE_NAME_OFDM:
        (void)bk7258_ofdm_power(enable);
        break;

      default:
        /* The RF table exposes the complete system-driver ABI, but this
         * target port only has verified raw gates for the Wi-Fi domains.
         * Retain an observable failure instead of silently voting a PM
         * module or pretending an unrelated domain was controlled. */
        ANALOG_UNPORTED("sys_drv_module_power_ctrl unsupported module");
        break;
    }
}

/* sys_hal_enter_low_analog()/sys_hal_exit_low_analog() are no longer defined
 * here (2026-09-09).  They were stubs, then briefly forwarders to a local
 * hand-port; both are superseded by the imported authority definitions in
 * pm/armino/middleware/soc/bk7258/hal/sys_pm_hal.c:1281,1294.  The RF
 * table (bk_rf_adapter.c:88-91) and the Wi-Fi funcs table
 * (bk7258_wifi_adapter.c) now reach that single definition.
 */

/****************************************************************************
 * Legacy driver-model control -- UNPORTED
 ****************************************************************************/

UINT32 ddev_control(DD_HANDLE handle, UINT32 cmd, void *param)
{
  /* The vendored PHY reaches SCTRL / ICU / BLE blocks through this. Refusing is
   * right: the alternative is dispatching commands we have not mapped.
   */

  (void)handle;
  (void)cmd;
  (void)param;
  ANALOG_UNPORTED("ddev_control");
  return (UINT32)BK_FAIL;
}

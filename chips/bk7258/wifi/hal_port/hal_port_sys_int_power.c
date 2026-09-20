/* hal_port_sys_int_power.c - MAC interrupt enables and module power-state
 * getters, ported from the Armino HAL/driver layer.
 *
 * Source chain (Armino pristine v3.1.1, cp/):
 *   components/bk_wifi/src/bk_wifi_adapter.c:768  bk_wifi_interrupt_init
 *   middleware/driver/sys_ctrl/sys_wifi_driver.c:581+
 *                                                 sys_drv_enable_mac_*_int
 *   middleware/driver/sys_ctrl/sys_ps_driver.c:330
 *                                                 sys_drv_module_power_state_get
 *   middleware/soc/bk7258/hal/sys_hal.c:1704-1732 sys_hal_enable_*_int
 *   middleware/soc/bk7258/hal/sys_hal.c:204-225   sys_hal_module_power_state_get
 *   components/bk_pm/pm.c:1309-1378               bk_pm_module_power_state_get
 *
 * Why this exists (two independent defects in the previous shims):
 *
 * 1. INTERRUPT BANK. Armino splits the seven MAC interrupt enables across
 *    two registers: gen/prot/tx_trigger/rx_trigger/txrx_misc live in
 *    `cpu0_int_32_63_en` (sources 32-36 = bits 0-4 of the HIGH bank) while
 *    txrx_timer (bit31) and modem (wifi_int_phy_mpb) live in
 *    `cpu0_int_0_31_en` (LOW bank).  Our previous bulk init ORed the bit
 *    constants {0,1,2,3,4,31} and passed them to a helper that writes only
 *    the LOW bank, so bit31 landed correctly but bits 0-4 enabled five
 *    unrelated LOW-bank sources.  The MAC interrupts themselves were never
 *    starved -- `_bk_int_isr_register` routes through bk7258_icu_enable(),
 *    which dispatches source>=32 to INT_EN_HI correctly (board: `ICU enable
 *    source=36`, `isr36=1`) -- so this is a pollution fix, not the FSM
 *    blocker.
 *
 * 2. POWER-STATE GETTERS. Both getters returned a compile-time constant,
 *    and they disagreed with each other: platform_shim returned
 *    PM_POWER_MODULE_STATE_NONE for every module while hw_driver_shim
 *    returned PM_POWER_MODULE_STATE_ON for every module, though both
 *    should report the same register.  The authoritative wrappers
 *    (bk_wifi_adapter.c:261-264 and :346-349) are UNGUARDED pass-throughs,
 *    unlike the SoC-gated `sys_ll_*_pwd_ofdm_wrapper`, so BK7258 really
 *    does reach the register-reading implementations copied below.
 *
 * Register-polarity note (do not "improve" this): the power register's
 * fields are named `pwd_*` / `pwr_dw` -- a SET bit means powered DOWN.  The
 * enum is PM_POWER_MODULE_STATE_ON == 0 / OFF == 1, so the raw bit already
 * IS the enum value and must be returned unmodified.  pm.c's own consumer
 * confirms the direction: `pm_module_check_power_on` powers a module up
 * only when the getter returns STATE_OFF, and vote_power_ctrl treats 0x0 as
 * "already on, nothing to do" (pm.c:529).
 */

#include <stdint.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <syslog.h>

/* bk_include.h first: modules/pm.h uses the vendor int32/uint32 aliases
 * that bk_typedef.h defines (same order as hal_port/platform_shim.c). */
#include <common/bk_include.h>
#include <common/bk_err.h>
#include <modules/pm.h>

#include "hal_port_sys_all.h"

/* PM_MODULE_SUB_POWER_DOMAIN_MAX and the PHY submodule ids come from
 * hal_port/include/common/sys_config.h via the vendor pm.h chain. */

/* ------------------------------------------------------------------ */
/* MAC / modem interrupt enables (sys_hal.c:1704-1732)                 */
/* ------------------------------------------------------------------ */

/* HIGH bank: cpu0_int_32_63_en (SYS reg 0x21 = 0x44010084) */

static void hp_sys_hal_enable_mac_gen_int(void)
{
  sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_gen_en(1);
}

static void hp_sys_hal_enable_mac_prot_int(void)
{
  sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_prot_trigger_en(1);
}

static void hp_sys_hal_enable_mac_tx_trigger_int(void)
{
  sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_tx_trigger_en(1);
}

static void hp_sys_hal_enable_mac_rx_trigger_int(void)
{
  sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_rx_trigger_en(1);
}

static void hp_sys_hal_enable_mac_txrx_misc_int(void)
{
  sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_tx_rx_misc_en(1);
}

/* LOW bank: cpu0_int_0_31_en (SYS reg 0x20 = 0x44010080) */

static void hp_sys_hal_enable_mac_txrx_timer_int(void)
{
  sys_ll_set_cpu0_int_0_31_en_cpu0_wifi_mac_int_tx_rx_timer_en(1);
}

static void hp_sys_hal_enable_modem_int(void)
{
  sys_ll_set_cpu0_int_0_31_en_cpu0_wifi_int_phy_mpb_en(1);
}

/* ------------------------------------------------------------------ */
/* bk_wifi_interrupt_init (bk_wifi_adapter.c:768-796)                  */
/* ------------------------------------------------------------------ */

/* Each sys_drv_enable_*_int wrapper (sys_wifi_driver.c:581+) is just the
 * HAL call inside a critical section; the whole sequence runs under one
 * critical section here, which is strictly stronger and avoids seven
 * enter/exit pairs.
 *
 * Config resolution for the guarded branches in the authoritative body:
 *   !CONFIG_SOC_BK7236A            -> txrx_misc IS enabled
 *   CONFIG_SOC_BK7236A             -> modem_rc NOT enabled
 *   CONFIG_SOC_BK7236XX && CFG_HSU -> HSU NOT enabled (CFG_HSU is
 *                                     commented out, rwnx_config.h:150)
 * so the live sequence is exactly the seven calls below.
 */

bk_err_t hp_bk_wifi_interrupt_init(void)
{
  irqstate_t flags = enter_critical_section();

  hp_sys_hal_enable_mac_gen_int();
  hp_sys_hal_enable_mac_prot_int();
  hp_sys_hal_enable_mac_tx_trigger_int();
  hp_sys_hal_enable_mac_rx_trigger_int();
  hp_sys_hal_enable_mac_txrx_misc_int();
  hp_sys_hal_enable_mac_txrx_timer_int();
  hp_sys_hal_enable_modem_int();

  leave_critical_section(flags);

  syslog(LOG_INFO,
         "[BK7258-WIFI] int init: lo=0x%08lx hi=0x%08lx\n",
         (unsigned long)REG_READ(SOC_SYS_REG_BASE + (0x20u << 2)),
         (unsigned long)REG_READ(SOC_SYS_REG_BASE + (0x21u << 2)));

  return BK_OK;
}

/* ------------------------------------------------------------------ */
/* sys_hal_clk_pwr_ctrl (sys_hal.c:1017-1040)                          */
/* ------------------------------------------------------------------ */

/* Generic device-clock gate, replacing a shim that hand-picked MAC and PHY
 * and returned BK_ERR_NOT_SUPPORT -- writing no register at all -- for every
 * other clock id.  Same defect class as the PHY_WIFI submodule vote: the
 * closed library asks for an id we never enumerated and the request is
 * silently dropped.  Known live victim: bk_phy_adapter.c:195 requests
 * PM_CLK_ID_SARADC (=5) on the phy_init path, and DEV_CLK_EN reads
 * 0x0c088284 on the board -- bit5 clear.
 *
 * Faithfulness check done before generalizing: Armino's accessor targets SYS
 * reg 0xc<<2 == SYS_BASE+0x30, the same register as our
 * BK7258_SYS_DEV_CLK_EN, and CLK_PWR_ID_MAC/PHY are 26/27 which are exactly
 * our MAC_CKEN/PHY_CKEN bit positions -- so `1 << id` reproduces the previous
 * MAC/PHY behavior bit-for-bit while also covering every other id.  Ids
 * arrive as raw numbers from the closed library and Armino indexes with them
 * unchanged, so no enum translation belongs here. */

#define HP_CLK_PWR_ID_H264   32   /* CLK_PWR_ID_H264 (sys_types.h) */

void hp_sys_hal_clk_pwr_ctrl(uint32_t dev, bool power_up)
{
  uint32_t offset;
  uint32_t v;

  if (dev >= HP_CLK_PWR_ID_H264)
    {
      offset = HP_CLK_PWR_ID_H264;
      v = sys_ll_get_reserver_reg0xd_value();
    }
  else
    {
      offset = 0;
      v = sys_ll_get_cpu_device_clk_enable_value();
    }

  if (power_up)
    {
      v |= (UINT32_C(1) << (dev - offset));
    }
  else
    {
      v &= ~(UINT32_C(1) << (dev - offset));
    }

  if (dev >= HP_CLK_PWR_ID_H264)
    {
      sys_ll_set_reserver_reg0xd_value(v);
    }
  else
    {
      sys_ll_set_cpu_device_clk_enable_value(v);
    }
}

/* ------------------------------------------------------------------ */
/* sys_hal_module_power_state_get (sys_hal.c:204-225)                  */
/* ------------------------------------------------------------------ */

int32_t hp_sys_hal_module_power_state_get(power_module_name_t module)
{
  uint32_t value;

  if (module >= POWER_MODULE_NAME_MEM1 && module <= POWER_MODULE_NAME_ROM_PGEN)
    {
      value = sys_ll_get_cpu_power_sleep_wakeup_value();
      value = (value >> module) & 0x1;
      return (int32_t)value;
    }
  else if (module == POWER_MODULE_NAME_CPU1)
    {
      return (int32_t)sys_ll_get_cpu_current_run_status_cpu1_pwr_dw_state();
    }
  else if (module == POWER_MODULE_NAME_CPU2)
    {
      return (int32_t)sys_ll_get_cpu_current_run_status_cpu2_pwr_dw_state();
    }
  else if (module == POWER_MODULE_NAME_TCM1_PGEN)
    {
      /* DEVIATION from verbatim: Armino's TCM1_PGEN arm is an unfinished
       * TODO whose body is `while(1){;}` -- copying it would hang the CP on
       * any query for this module.  Nothing on the Wi-Fi path queries it;
       * log once and fall through to the function's own `return 0`. */

      static bool reported;

      if (!reported)
        {
          reported = true;
          syslog(LOG_WARNING,
                 "[BK7258-WIFI] power_state_get: TCM1_PGEN unimplemented "
                 "upstream (Armino hangs here); returning 0\n");
        }

      return 0;
    }

  return 0;
}

/* sys_drv_module_power_state_get (sys_ps_driver.c:330-333) */

int32_t hp_sys_drv_module_power_state_get(power_module_name_t module)
{
  return hp_sys_hal_module_power_state_get(module);
}

/* ------------------------------------------------------------------ */
/* bk_pm_module_power_state_get (pm.c:1309-1378)                       */
/* ------------------------------------------------------------------ */

/* Armino resolves a submodule id to its parent power domain through five
 * explicit if-chains (BAKP, AHBP, AUDP, VIDP, PHY).  Every submodule id is
 * `parent * PM_MODULE_SUB_POWER_DOMAIN_MAX + k`, so the chains are
 * expressed here as the equivalent id ranges rather than 43 enum constants
 * this tree does not carry.
 *
 * The ranges are deliberately NOT generalized to `module / 20`: ENCP also
 * has submodules (ENCP_OTP=60, ENCP_TRUSTENGINE=61) and PHY has a third
 * (PHY_RF=202), and the authoritative getter does NOT list those -- they
 * fall through to the `>= NONE` arm and return BK_ERR_NOT_SUPPORT.  A
 * generic division would silently answer for them instead.
 */

#define HP_SUB_BASE(parent)  ((parent) * PM_MODULE_SUB_POWER_DOMAIN_MAX)

int32_t hp_bk_pm_module_power_state_get(pm_power_module_name_e module)
{
  uint32_t id = (uint32_t)module;

  /* BAKP_TIMER1 .. BAKP_PM (BAKP_NONE excluded, as upstream) */

  if (id >= HP_SUB_BASE(PM_POWER_MODULE_NAME_BAKP) &&
      id <= HP_SUB_BASE(PM_POWER_MODULE_NAME_BAKP) + 16)
    {
      return hp_sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_BAKP);
    }

  /* AHBP_CAN .. AHBP_LIN */

  if (id >= HP_SUB_BASE(PM_POWER_MODULE_NAME_AHBP) &&
      id <= HP_SUB_BASE(PM_POWER_MODULE_NAME_AHBP) + 7)
    {
      return hp_sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_AHBP);
    }

  /* AUDP_FFT .. AUDP_I2S */

  if (id >= HP_SUB_BASE(PM_POWER_MODULE_NAME_AUDP) &&
      id <= HP_SUB_BASE(PM_POWER_MODULE_NAME_AUDP) + 3)
    {
      return hp_sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_AUDP);
    }

  /* VIDP_JPEG_EN .. VIDP_H264 */

  if (id >= HP_SUB_BASE(PM_POWER_MODULE_NAME_VIDP) &&
      id <= HP_SUB_BASE(PM_POWER_MODULE_NAME_VIDP) + 8)
    {
      return hp_sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_VIDP);
    }

  /* PHY_BT, PHY_WIFI (PHY_RF is NOT listed upstream) */

  if (id == (uint32_t)POWER_SUB_MODULE_NAME_PHY_BT ||
      id == (uint32_t)POWER_SUB_MODULE_NAME_PHY_WIFI)
    {
      return hp_sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_PHY);
    }

  if (id >= (uint32_t)PM_POWER_MODULE_NAME_NONE)
    {
      syslog(LOG_DEBUG,
             "[BK7258-WIFI] power_state_get: module %lu not supported\n",
             (unsigned long)id);
      return BK_ERR_NOT_SUPPORT;
    }

  return hp_sys_drv_module_power_state_get((power_module_name_t)id);
}

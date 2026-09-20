/*
 * BK7258 platform callbacks still referenced by the vendor capability tables.
 *
 * Policy: use NuttX equivalents where semantics are clear; return an explicit
 * unsupported error where a power/PM contract has not been ported. No fake
 * success for hardware control.
 */

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/clock.h>
#include <arm_internal.h>
#include <arch/chip/bk7258_clock.h>
#include <arch/chip/bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <modules/pm.h>

#include "driver/aon_rtc.h"
#include "os/os.h"

static bool g_phy_reinit;
/* The authority stores low-voltage callbacks by PM device ID.  A single slot
 * per sleep mode loses SARADC's callback when Wi-Fi registers its own MAC
 * callback, so retain the same ownership dimension even though NuttX does not
 * yet execute an Armino low-voltage transition. */
static pm_sleep_cb_t g_pm_sleep_enter[PM_MODE_DEFAULT][PM_DEV_ID_MAX];
static pm_sleep_cb_t g_pm_sleep_exit[PM_MODE_DEFAULT][PM_DEV_ID_MAX];
/* The CP has no owner for the external-32k mux yet.  Preserve registrations
 * faithfully for that future owner, but do not synthesize a source switch or
 * invoke a registered callback from this layer.  volatile makes this a real
 * retained state transition rather than an optimizer-elided success path. */
static volatile pm_cb_extern32k_cfg_t g_extern32k_cfg[PM_32K_MODULE_MAX];
static volatile bool g_extern32k_registered[PM_32K_MODULE_MAX];
static bool g_coex_wifi_open;

static pm_lpo_src_e bk7258_wifi_lpo_src_read(void)
{
  uint32_t source = getreg32(BK7258_AON_PMU_R41) &
                    BK7258_AON_PMU_R41_LPO_CONFIG_MASK;

  /* PM_LPO_SRC_DEFAULT is an enum sentinel, not a hardware encoding.  The
   * documented BK7258 field accepts DIVD, external 32K, or ROSC only. */
  if (source > (uint32_t)PM_LPO_SRC_ROSC)
    {
      return PM_LPO_SRC_ROSC;
    }

  return (pm_lpo_src_e)source;
}

/* bk_aon_rtc_get_current_tick() and bk_rtc_get_ms_tick_count() used to be
 * defined here.  Both now come from the authority AON RTC module imported under
 * chips/bk7258/aon_rtc/ (authority/middleware/driver/rtc/aon_rtc_driver_64bit.c
 * :1111 and :132), which additionally owns the counter enable/start sequence
 * that the read-only version here could not perform.
 *
 * The tick rate is unchanged by the switch.  This port returned 32.0f for a
 * non-X32K LPO source; the authority version returns s_aon_rtc_clock_freq/1000,
 * and with CONFIG_EXTERN_32K unset (authority BK7258 CP) s_aon_rtc_clock_freq
 * is AON_RTC_DEFAULT_CLOCK_FREQ == 32000, i.e. also 32.0f.  This matters
 * because libwifi.a's rwnxl_sleep() derives its poll timeout from this rate.
 * The authority value is a build-time default rather than a live PMU read, so
 * it no longer tracks a runtime LPO source change; the CP votes ROSC once
 * during bring-up (bk7258_wifi_lower.c) and does not switch afterwards, and the
 * authority firmware itself has the same static behavior via
 * CONFIG_DEFAULT_LPO_SRC.
 */

bk_err_t bk_pm_clock_ctrl(pm_dev_clk_e module, pm_dev_clk_pwr_e clock_state)
{
  bool enable;

  if (clock_state == PM_CLK_CTRL_PWR_UP)
    {
      enable = true;
    }
  else if (clock_state == PM_CLK_CTRL_PWR_DOWN)
    {
      enable = false;
    }
  else
    {
      return BK_ERR_PARAM;
    }

  /* Generic gate, ported from sys_hal_clk_pwr_ctrl (see the faithfulness
   * note in hal_port/hal_port_sys_int_power.c).  Previously this hand-picked
   * MAC and PHY and returned BK_ERR_NOT_SUPPORT for every other id without
   * writing a register -- so bk_phy_adapter.c:195's PM_CLK_ID_SARADC request
   * on the phy_init path was silently dropped.  MAC/PHY keep the same
   * resulting bits (ids 26/27 == MAC_CKEN/PHY_CKEN). */

  /* One-shot per id: record WHICH clock ids the closed library actually asks
   * for.  Before this function was generalized every id other than MAC(26)
   * and PHY(27) was dropped with BK_ERR_NOT_SUPPORT and no register write, so
   * the set of victims was never observable.  Now that the write always
   * happens, this log is the only way to learn what had been silently lost
   * (SARADC=5 is the one already proven from source; XVR=25 and BTDM=24 are
   * the other plausible requesters on the RF path). */

  {
    static uint32_t reported;
    uint32_t id = (uint32_t)module;

    if (id < 32 && (reported & (UINT32_C(1) << id)) == 0)
      {
        reported |= UINT32_C(1) << id;
        syslog(LOG_INFO, "[BK7258-WIFI] clk id=%lu en=%d first\n",
               (unsigned long)id, (int)enable);
      }
  }

  hp_sys_hal_clk_pwr_ctrl((uint32_t)module, enable);
  return BK_OK;
}

bk_err_t bk_pm_lpo_src_set(pm_lpo_src_e lpo_src)
{
  /* Armino aon_pmu_hal_lpo_src_set: read-modify-write only the
   * lpo_config field (R41 bits[1:0]), preserving the rest. */
  uint32_t r41 = getreg32(BK7258_AON_PMU_R41);

  r41 = (r41 & ~BK7258_AON_PMU_R41_LPO_CONFIG_MASK) |
        ((uint32_t)lpo_src & BK7258_AON_PMU_R41_LPO_CONFIG_MASK);
  putreg32(r41, BK7258_AON_PMU_R41);
  return BK_OK;
}

pm_lpo_src_e bk_pm_lpo_src_get(void)
{
  /* The PMU R41 field is the live BK7258 LPO source.  This query does not
   * claim ownership of the mux or alter its selection. */
  return bk7258_wifi_lpo_src_read();
}

/* Real register-backed power-state query, ported from pm.c:1309-1378 into
 * hal_port/hal_port_sys_int_power.c.  The former fixed
 * PM_POWER_MODULE_STATE_NONE return also contradicted hw_driver_shim's
 * fixed STATE_ON, though both slots report the same register. */
extern int32_t hp_bk_pm_module_power_state_get(pm_power_module_name_e module);
extern void hp_sys_hal_clk_pwr_ctrl(uint32_t dev, bool power_up);

/* Declared in hal_port/include/sys_driver.h, which this file does not include;
 * the PHY submodule vote below needs it to read back the power domain the
 * way pm.c:634 does. */
extern int32_t sys_drv_module_power_state_get(power_module_name_t module);

/* Keep the direct PHY module vote and PHY submodule votes in one ownership
 * state machine.  Armino's pm.c records PHY calibration when the direct PHY
 * vote succeeds; the following PHY_WIFI vote then observes an already-powered,
 * calibrated domain and must not replay phy_wakeup_reinit(). */

static uint32_t g_phy_submodule_state;
static uint32_t g_phy_calibration_state;
static spinlock_t g_phy_power_lock = SP_UNLOCKED;
static uint32_t g_bakp_submodule_state;
static spinlock_t g_bakp_power_lock = SP_UNLOCKED;

int32 bk_pm_module_power_state_get(pm_power_module_name_e module)
{
  return (int32)hp_bk_pm_module_power_state_get(module);
}

/* Real DVFS, ported from the Armino HAL (hal_port/hal_port_dvfs.c).
 * Replaces the former accept-and-log stub: that stub left the core voltage
 * at whatever the bootloader set, so the authoritative pairing of 120M with
 * vdddig 0.9V (sys_hal.c:663) never happened on our board. */
extern bk_err_t hp_pm_module_vote_cpu_freq(pm_dev_id_e module,
                                           pm_cpu_freq_e cpu_freq);

bk_err_t bk_pm_module_vote_cpu_freq(pm_dev_id_e module, pm_cpu_freq_e cpu_freq)
{
  bk_err_t ret = hp_pm_module_vote_cpu_freq(module, cpu_freq);

  syslog(LOG_INFO,
         "[BK7258-WIFI] pmq: cpu_freq vote module=%d freq=%d ret=%d\n",
         (int)module, (int)cpu_freq, (int)ret);
  return ret;
}

bk_err_t bk_pm_module_vote_power_ctrl(pm_power_module_name_e module,
                                       pm_power_module_state_e power_state)
{
  bool enable;

  if (power_state == PM_POWER_MODULE_STATE_ON)
    {
      enable = true;
    }
  else if (power_state == PM_POWER_MODULE_STATE_OFF)
    {
      enable = false;
    }
  else
    {
      return BK_ERR_PARAM;
    }

  if (module == PM_POWER_MODULE_NAME_WIFIP_MAC)
    {
      return bk7258_mac_power(enable) == OK ? BK_OK : BK_FAIL;
    }
  else if ((uint32_t)module >=
             (uint32_t)PM_POWER_MODULE_NAME_BAKP *
             PM_MODULE_SUB_POWER_DOMAIN_MAX &&
           (uint32_t)module <=
             (uint32_t)PM_POWER_MODULE_NAME_BAKP *
             PM_MODULE_SUB_POWER_DOMAIN_MAX + 16)
    {
      /* Armino pm.c:504-536 and :726-755: BAKP submodules hold their
       * parent domain until the final user releases it.  This is required
       * by the BK7258 authority SARADC PM callback path. */
      const uint32_t bit = UINT32_C(1) <<
        ((uint32_t)module %
         ((uint32_t)PM_POWER_MODULE_NAME_BAKP *
          PM_MODULE_SUB_POWER_DOMAIN_MAX));
      irqstate_t flags = spin_lock_irqsave(&g_bakp_power_lock);

      if (enable)
        {
          g_bakp_submodule_state |= bit;
          spin_unlock_irqrestore(&g_bakp_power_lock, flags);

          if (sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_BAKP) == 0x0)
            {
              return BK_OK;
            }

          return bk7258_bakp_power(true) == OK ? BK_OK : BK_FAIL;
        }

      g_bakp_submodule_state &= ~bit;
      if (g_bakp_submodule_state == 0)
        {
          spin_unlock_irqrestore(&g_bakp_power_lock, flags);
          return bk7258_bakp_power(false) == OK ? BK_OK : BK_FAIL;
        }

      spin_unlock_irqrestore(&g_bakp_power_lock, flags);
      return BK_OK;
    }
  else if (module == PM_POWER_MODULE_NAME_PHY)
    {
      irqstate_t flags;

      /* Authority pm.c permits direct PHY ON for PHY initialization and
       * calibration, but only submodule votes may turn it back off. */
      if (!enable)
        {
          return BK_FAIL;
        }

      if (bk7258_phy_power(true) != OK)
        {
          return BK_FAIL;
        }

      flags = spin_lock_irqsave(&g_phy_power_lock);
      g_phy_calibration_state = 0x1;
      spin_unlock_irqrestore(&g_phy_power_lock, flags);
      return BK_OK;
    }
  else if (module == (pm_power_module_name_e)POWER_SUB_MODULE_NAME_PHY_BT ||
           module == (pm_power_module_name_e)POWER_SUB_MODULE_NAME_PHY_WIFI ||
           module == (pm_power_module_name_e)POWER_SUB_MODULE_NAME_PHY_RF)
    {
      /* PHY submodule vote, ported from pm.c:628-660 (ON) and :815-830
       * (OFF).  This arm previously fell through to BK_ERR_NOT_SUPPORT, so
       * wifi_init.c:71's third power vote never did anything -- and with it
       * we skipped the ONE authoritative side effect on this path:
       * phy_wakeup_reinit().  That function is defined in the pinned
       * libwifi.a (phy_karst_bk7236.c.obj) and our port had never called it.
       *
       * Refcount semantics matter in both directions: ON is idempotent only
       * once calibration has run, and OFF powers the PHY down solely when
       * the last submodule releases it (wifi_v2.c:1878 votes PHY_WIFI OFF,
       * which must NOT kill a PHY that BT/RF still hold).
       *
       * Polarity reminder: sys_drv_module_power_state_get returns the raw
       * pwd bit, so 0x0 means "powered ON" -- the comparisons below read
       * exactly as upstream writes them. */

      const uint32_t bit = UINT32_C(1) <<
        ((uint32_t)module % (uint32_t)(PM_POWER_MODULE_NAME_PHY *
                                       PM_MODULE_SUB_POWER_DOMAIN_MAX));
      irqstate_t flags;

      if (enable)
        {
          flags = spin_lock_irqsave(&g_phy_power_lock);
          g_phy_submodule_state |= bit;
          spin_unlock_irqrestore(&g_phy_power_lock, flags);

          if (sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_PHY) == 0x0 &&
              g_phy_calibration_state == 0x1)
            {
              return BK_OK;
            }

          if (bk7258_phy_power(true) != OK)
            {
              return BK_FAIL;
            }

          if (sys_drv_module_power_state_get(PM_POWER_MODULE_NAME_PHY) == 0x0 &&
              module != (pm_power_module_name_e)POWER_SUB_MODULE_NAME_PHY_RF)
            {
              extern void phy_wakeup_reinit(uint8_t is_wifi);

              syslog(LOG_INFO,
                     "[BK7258-WIFI] pmq: phy_wakeup_reinit(is_wifi=%d) enter\n",
                     module ==
                     (pm_power_module_name_e)POWER_SUB_MODULE_NAME_PHY_WIFI);

              phy_wakeup_reinit(module ==
                (pm_power_module_name_e)POWER_SUB_MODULE_NAME_PHY_WIFI ? 1 : 0);

              syslog(LOG_INFO,
                     "[BK7258-WIFI] pmq: phy_wakeup_reinit done\n");

              g_phy_reinit = true;

              flags = spin_lock_irqsave(&g_phy_power_lock);
              g_phy_calibration_state = 0x1;
              spin_unlock_irqrestore(&g_phy_power_lock, flags);
            }

          return BK_OK;
        }

      flags = spin_lock_irqsave(&g_phy_power_lock);
      g_phy_submodule_state &= ~bit;

      if (g_phy_submodule_state == 0x0)
        {
          g_phy_calibration_state = 0x0;
          spin_unlock_irqrestore(&g_phy_power_lock, flags);
          return bk7258_phy_power(false) == OK ? BK_OK : BK_FAIL;
        }

      spin_unlock_irqrestore(&g_phy_power_lock, flags);
      return BK_OK;
    }

  /* Report instead of dropping silently.  This exact fallthrough hid a real
   * bug: PHY_WIFI was mis-defined as 3, landed here, and wifi_init.c:71's
   * third power vote wrote no register at all for weeks.  Any further module
   * the closed library votes for and we do not recognize must be visible.
   * One-shot per module id, so a repeated vote cannot flood the console. */

  {
    static uint32_t reported_lo;
    static uint32_t reported_hi;
    uint32_t id = (uint32_t)module;
    uint32_t *slot = id < 32 ? &reported_lo : &reported_hi;
    uint32_t bit = UINT32_C(1) << (id < 32 ? id : (id % 32));

    if ((*slot & bit) == 0)
      {
        *slot |= bit;
        syslog(LOG_WARNING, "[BK7258-WIFI] pwr vote id=%lu UNHANDLED\n",
               (unsigned long)id);
      }
  }

  return BK_ERR_NOT_SUPPORT;
}

bk_err_t bk_pm_module_vote_sleep_ctrl(pm_sleep_module_name_e module,
                                       uint32_t sleep_state,
                                       uint32_t sleep_time)
{
  /* Behavioral alignment with pm.c bk_pm_module_vote_sleep_ctrl plus
   * sys_pm_hal enable/clear_mac_wakeup_source: maintain the slept-module
   * bitmap and operate the WIFI bit inside the AON PMU R41 wakeup_ena
   * field (bits[9:4]; WAKEUP_SOURCE_INT_WIFI=2 -> register bit 6).  The
   * RMW preserves every other R41 field (lpo_config, xtal_sel, halt_*). */
  static uint64_t slept_modules;
  static spinlock_t lock = SP_UNLOCKED;
  const uint32_t r41_wifi_bit = UINT32_C(1) << (4 + 2);
  irqstate_t flags;
  uint32_t r41;

  flags = spin_lock_irqsave(&lock);

  if (sleep_state == 1)
    {
      if (module == PM_SLEEP_MODULE_NAME_WIFIP_MAC)
        {
          r41 = getreg32(BK7258_AON_PMU_R41);
          r41 |= r41_wifi_bit;
          putreg32(r41, BK7258_AON_PMU_R41);
        }
      slept_modules |= UINT64_C(1) << module;
    }
  else
    {
      if (module == PM_SLEEP_MODULE_NAME_WIFIP_MAC)
        {
          r41 = getreg32(BK7258_AON_PMU_R41);
          r41 &= ~r41_wifi_bit;
          putreg32(r41, BK7258_AON_PMU_R41);
        }
      slept_modules &= ~(UINT64_C(1) << module);
    }

  spin_unlock_irqrestore(&lock, flags);

  /* The per-vote record is dropped.  This function has no failure path -- it
   * always returns BK_OK -- so the line carried no error information, but the
   * temperature/voltage sampler votes the SARADC module asleep and awake on
   * every cycle, which made it one of the loudest sources in a capture and
   * pushed the wpa_supplicant association trace out of the visible window. */

  return BK_OK;
}

void bk_pm_phy_reinit_flag_clear(void)
{
  g_phy_reinit = false;
}

bool bk_pm_phy_reinit_flag_get(void)
{
  return g_phy_reinit;
}

bk_err_t bk_pm_sleep_register_cb(pm_sleep_mode_e sleep_mode,
                                  pm_dev_id_e dev_id,
                                  pm_cb_conf_t *enter_config,
                                  pm_cb_conf_t *exit_config)
{
  if (sleep_mode >= PM_MODE_DEFAULT)
    {
      return BK_ERR_PARAM;
    }

  if ((uint32_t)dev_id >= PM_DEV_ID_MAX)
    {
      return BK_ERR_PARAM;
    }

  /* NuttX currently has no mapped BK sleep transition.  Retaining these
   * source-backed registrations is nevertheless required so a later owned
   * transition driver can invoke exactly the callback and opaque argument
   * supplied by the Wi-Fi/PHY source. */
  if (enter_config != NULL && enter_config->cb != NULL)
    {
      g_pm_sleep_enter[sleep_mode][dev_id].id = dev_id;
      g_pm_sleep_enter[sleep_mode][dev_id].cfg = *enter_config;
    }

  if (exit_config != NULL && exit_config->cb != NULL)
    {
      g_pm_sleep_exit[sleep_mode][dev_id].id = dev_id;
      g_pm_sleep_exit[sleep_mode][dev_id].cfg = *exit_config;
    }

  return BK_OK;
}

bk_err_t bk_pm_sleep_unregister_cb(pm_sleep_mode_e sleep_mode,
                                   pm_dev_id_e dev_id,
                                   bool enter_cb, bool exit_cb)
{
  irqstate_t flags;

  if (sleep_mode >= PM_MODE_DEFAULT || (uint32_t)dev_id >= PM_DEV_ID_MAX)
    {
      return BK_ERR_PARAM;
    }

  flags = enter_critical_section();
  if (enter_cb)
    {
      g_pm_sleep_enter[sleep_mode][dev_id].id = PM_DEV_ID_MAX;
      g_pm_sleep_enter[sleep_mode][dev_id].cfg.cb = NULL;
      g_pm_sleep_enter[sleep_mode][dev_id].cfg.args = NULL;
    }

  if (exit_cb)
    {
      g_pm_sleep_exit[sleep_mode][dev_id].id = PM_DEV_ID_MAX;
      g_pm_sleep_exit[sleep_mode][dev_id].cfg.cb = NULL;
      g_pm_sleep_exit[sleep_mode][dev_id].cfg.args = NULL;
    }

  leave_critical_section(flags);
  return BK_OK;
}

/* wifi_v2.c calls these two exported wrappers directly during bk_wifi_init().
 * Keep the upstream adapter's registration semantics while leaving actual
 * sleep-state transitions to the future NuttX PM owner.  In particular, do
 * not report a callback as executed: bk_pm_sleep_register_cb only records it.
 */
bk_err_t bk_pm_sleep_register_wrapper(void *config_cb)
{
  pm_cb_conf_t enter_config_wifi = {NULL, NULL};

  enter_config_wifi.cb = config_cb;
#if !CONFIG_PM_SUPER_DEEP_SLEEP
  return bk_pm_sleep_register_cb(PM_MODE_DEEP_SLEEP, PM_DEV_ID_MAC,
                                 &enter_config_wifi, NULL);
#else
  /* Match the authority wrapper: retain both registrations.  The NuttX PM
   * owner still decides when a system sleep transition may occur; this layer
   * does not claim such a transition happened. */
  (void)bk_pm_sleep_register_cb(PM_MODE_DEEP_SLEEP, PM_DEV_ID_MAC,
                                &enter_config_wifi, NULL);
  (void)bk_pm_sleep_register_cb(PM_MODE_SUPER_DEEP_SLEEP, PM_DEV_ID_MAC,
                                &enter_config_wifi, NULL);
  return BK_OK;
#endif
}

bk_err_t bk_pm_low_voltage_register_wrapper(void *config_cb)
{
  pm_cb_conf_t wifi_exit_config = {NULL, NULL};

  wifi_exit_config.cb = config_cb;
  return bk_pm_sleep_register_cb(PM_MODE_LOW_VOLTAGE, PM_DEV_ID_MAC, NULL,
                                 &wifi_exit_config);
}

bk_err_t pm_extern32k_register_cb(pm_cb_extern32k_cfg_t *cfg)
{
  if (cfg == NULL || cfg->cb_module >= PM_32K_MODULE_MAX ||
      cfg->cb_func == NULL)
    {
      return BK_ERR_PARAM;
    }

  g_extern32k_cfg[cfg->cb_module] = *cfg;
  g_extern32k_registered[cfg->cb_module] = true;
  return BK_OK;
}

void bk_task_wdt_feed(void)
{
  /* NuttX watchdog ownership is outside this Wi-Fi port; no task watchdog is
   * armed by the current BK7258 CP configuration, so there is nothing to feed.
   */
}

void bk_system_dump(const char *func, const int line)
{
  (void)func;
  (void)line;
}

bool ate_is_enabled(void)
{
  return false;
}

bool ble_in_dut_mode(void)
{
  return false;
}

void coex_ictw_report_wifi_open_status(bool is_wifi_open)
{
  /* Armino's wifi_init.c only publishes this open/closed status.  Keep the
   * state for a future coex owner; this does not claim BT/PTA control. */
  g_coex_wifi_open = is_wifi_open;
}

/* libwifi.a was built with coex notification calls enabled.  This STA-only
 * build has no BT/PTA owner or registered recipient, so retain the ABI but do
 * not create policy, schedule BT work, or claim coexistence support. */
void coex_ictw_report_wifi_sleep_status(bool is_wifi_sleeping)
{
  (void)is_wifi_sleeping;
}

void coex_ictw_report_wifi_traffic(volatile uint32_t cntx_id,
                                   uint32_t level_idx)
{
  (void)cntx_id;
  (void)level_idx;
}

void coex_ictw_report_wifi_scan_status(volatile uint32_t cntx_id,
                                       bool is_scan_on)
{
  (void)cntx_id;
  (void)is_scan_on;
}

void coex_ictw_report_wifi_connecting_status(volatile uint32_t cntx_id,
                                             bool is_connecting)
{
  (void)cntx_id;
  (void)is_connecting;
}

void coex_ictw_report_wifi_connected_status(volatile uint32_t cntx_id,
                                            bool is_connected)
{
  (void)cntx_id;
  (void)is_connected;
}

bk_err_t cif_handle_bk_cmd_csi_info_ind(void *data)
{
  /* CSI/controller interface is not part of the STA MVP. Explicitly reject. */
  (void)data;
  return BK_ERR_NOT_SUPPORT;
}

bk_err_t cif_handle_bk_cmd_bcn_cc_ind(uint8_t *cc, uint8_t cc_len)
{
  /* Controller-side beacon country-code indication. This port has no
   * controller_if process; country code is configured through wireless_ops.
   */
  (void)cc;
  (void)cc_len;
  return BK_ERR_NOT_SUPPORT;
}

void shell_log_flush(void)
{
  /* NuttX stdio is synchronous in this port; no async shell ring to flush. */
}

void os_dump_memory_stats(uint32_t start_tick, uint32_t ticks_since_malloc,
                          const char *task)
{
  /* Debug-only API called by hostapd_intf when CONFIG_MEM_DEBUG is enabled.
   * NuttX heap accounting is already available through kmm_mallinfo(); keep
   * this hook side-effect free until it is routed to that diagnostic surface.
   */
  (void)start_tick;
  (void)ticks_since_malloc;
  (void)task;
}

void os_show_memory_config_info(void)
{
  /* Same debug-only rationale as os_dump_memory_stats(). */
}

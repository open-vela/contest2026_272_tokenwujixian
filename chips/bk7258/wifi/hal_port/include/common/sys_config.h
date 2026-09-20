/*
 * chips/bk7258/wifi/hal_port/include/common/sys_config.h
 *
 * NuttX reimplementation of the Armino build-config header. Maps the Armino
 * CONFIG_* feature macros onto the BK7258 NuttX adaptation defaults. Defaults
 * reflect the STA MVP (plan §11): Wi-Fi enabled, no lwIP, no FreeRTOS, no
 * P2P/bridge/virtual-controller. Values are refined as the vendored sources
 * are compiled against NuttX.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_SYS_CONFIG_H
#define __BK7258_WIFI_GLUE_COMMON_SYS_CONFIG_H

#include <nuttx/config.h>

/* SoC family switches.  BK7258 belongs to the BK7236XX family: the
 * authoritative BK7258 project configs set CONFIG_SOC_BK7236XX=y (loader
 * ChipId 0x7236; see the armino bk7258 project configs).  Keeping it 0
 * compiled every vendored 7236XX-conditional branch out, diverging from
 * the profile the pinned libwifi.a was shipped for. */
#define CONFIG_SOC_BK7236XX         1
#define CONFIG_SOC_BK7239XX         0
#define CONFIG_SOC_BK7256XX         0
#define CONFIG_SOC_BK7286XX         0

/* Runtime/hosting model: MAC/PHY runs on this core (NO_HOSTED), NuttX owns
 * the network stack (no lwIP), no second scheduler (no FreeRTOS SMP). */
#define CONFIG_NO_HOSTED            1
#define CONFIG_FULLY_HOSTED         0
#define CONFIG_SEMI_HOSTED          0
/* DELIBERATE DIVERGENCE from the authority (which sets CONFIG_LWIP=1).
 * NuttX owns the network stack; the RX path must not route through
 * pbuf_alloc()/lwIP (rwnx_rx.c:788 fhost_rxbuf_push, :828
 * fhost_free_rx_buffer).  Keeping 0 selects the ke_malloc/ke_free arms, which
 * is what our pbuf shim is built around.  Do NOT "align" this one. */
#define CONFIG_LWIP                 0

/* NOT DEFINED, DELIBERATELY (2026-09-01): the authority's sdkconfig.h does not
 * define CONFIG_FREERTOS_SMP at all, and every site that tests it uses
 * `#ifdef` (bk_phy_adapter.c:47,248; bk_rf_adapter.c:16,47,53,61 -- 8 sites, 0
 * value-style).  `#define ... 0` makes `#ifdef` TRUE, so we were compiling the
 * SMP spinlock arms of rtos_disable_int_wrapper()/rtos_enable_int_wrapper()
 * that the authority never compiles.  Leaving it undefined is what makes those
 * blocks disappear; see the "#define 0 is not 'off'" note at the end of this
 * file.
 *
 * #define CONFIG_FREERTOS_SMP      0   <-- must stay commented out
 */

/* Wi-Fi core. */
#define CONFIG_WIFI_ENABLE          1
#define CONFIG_WIFI6                1

/* PM alignment with the successful BK7258 Armino iperf image
 * (build/bk7258/iperf/bk7258/config/sdkconfig.h).  These gates select the
 * same Wi-Fi core-thread and LMAC configuration branches as the authority:
 *
 *   PM_V2: core thread wakes the MAC/RF before every work item and evaluates
 *          the closed ps.c sleep state machine afterwards;
 *   STA_PS: enables PS messages and station-PS API paths;
 *   MCU_PS: exposes the authority's MCU-PS API paths (its adapter callbacks
 *           are intentionally empty in the authority too);
 *   PM_ENABLE/SUPER_DEEP_SLEEP: preserve authority registration behaviour.
 *
 * The invoked MAC doze/wake state machine is from the hash-pinned libwifi.a.
 * Its external dependency closure is provided by this port before these gates
 * are enabled; do not replace any authority-empty power_save_* callback with
 * invented behaviour. */
#define CONFIG_PM_V2                 1
#define CONFIG_STA_PS                1
#define CONFIG_MCU_PS                1
#define CONFIG_PM_ENABLE             1
#define CONFIG_PM_SUPER_DEEP_SLEEP   1
#define CONFIG_DEFAULT_LPO_SRC       2

/* Authority BK7258 sdkconfig.h enables the PHY temperature-compensation
 * callback.  Without this, bk_feature_temp_detect_enable() returns 0 and the
 * closed PHY bypasses the chip-owned temp_detect implementation entirely. */
#define CONFIG_TEMP_DETECT          1
/* Authority BK7258 defconfig:180.  The NuttX tempd worker owns the matching
 * ADC0 periodic sample and rwnx_cal_do_volt_detect() callback. */
#define CONFIG_VOLT_DETECT          1
#define CONFIG_CALI                 1
#define CONFIG_MANUAL_CALI          1
#define CONFIG_TPC_PA_MAP           1
#define CONFIG_SARADC_CALI          1
#define CONFIG_SARADC               1
#define CONFIG_SARADC_SERVER        1
/* Do not activate authority multi-core feature macros independently. Their
 * mailbox_channel and mb_ipc state machines now have an RPMsg backend, but
 * PHY/SARADC clients and CPU0 servers are enabled together only after their
 * full authority ABI and host lifecycle contracts are integrated. This image
 * therefore does not define CONFIG_CPU_CNT or advertise CPU2. */
#define CONFIG_PHY_MB               0
#define CONFIG_SARADC_MB            0
/* Complete-SDK BK7258 defconfig enables OTP v1 and leaves
 * CONFIG_PHY_RFCALI_TO_OTP=n. The authority driver/HAL/LL/map closure owns
 * both read and write APIs; the default Wi-Fi profile does not elect RF
 * calibration persistence, matching the SDK rather than refusing updates in
 * a local shim. */
#define CONFIG_OTP_V1               1
/* BK7258 authority defconfig:184.  This selects adc_restore plus the BAKP
 * SARADC power-vote lifecycle; platform_shim.c provides the matching PM
 * registration, unregistration, and parent-domain reference counting. */
#define CONFIG_SARADC_PM_CB_SUPPORT 1
/* Authority BK7258 iperf config leaves CONFIG_SARADC_NEED_FLUSH undefined.
 * Defining it starts bk_adc_init() with adc_flush() before adc_hal_init()
 * assigns s_adc.hal.hw, which dereferences the zeroed HAL pointer. */
/* #define CONFIG_SARADC_NEED_FLUSH 1 */
#define CONFIG_ADC_BUF_SIZE         32
#define CONFIG_ADC_STATIS           1
#define CONFIG_SYSTEM_CTRL          1

/* Low-power core voltage level, consumed by sys_hal_low_power_hardware_init()
 * as sys_hal_lp_vol_set(CONFIG_LP_VOL).  0x6 matches the authority BK7258 CP
 * project config (projects/app/cp/config/bk7258/config:941). */
#define CONFIG_LP_VOL               0x6

/* Core clock the imported sys_hal.c assumes when it programs the MCLK divider
 * (sys_hal.c:2734, 480000000/CONFIG_CPU_FREQ_HZ - 1).  120 MHz matches the
 * authority BK7258 CP project config (:673) and the 120M vote this port makes
 * before any PHY/calibration path runs. */
#define CONFIG_CPU_FREQ_HZ          120000000

/* Authority BK7258 CP sets CONFIG_MAILBOX=y.  pwr_clk.c guards its
 * <driver/mailbox_channel.h> include on it (:17) while using MB_CHNL_PWC /
 * MB_CHNL_GET_STATUS unconditionally in bk_pm_cp_mb_busy() (:763), so the
 * imported PM sources need it set to compile the same way upstream does.
 * Everything it enables is self-contained: the three CP0 mailbox ISRs and
 * PM_CHNL_STATE_BUSY are defined inside pwr_clk.c, and mb_chnl_open/ctrl/write
 * already link from the imported mb_ipc mailbox driver.  No file outside
 * pm/authority tests this symbol, so nothing else changes behaviour. */
#define CONFIG_MAILBOX              1

/* AON RTC configuration gate for the directly imported BK7258 authority module
 * under chips/bk7258/aon_rtc/.  The values (and the authority defconfig
 * provenance of each one) live in that module's own port header, but they are
 * reached from here rather than force-included for the module alone, because
 * CONFIG_AON_RTC_64BIT selects the rtc_tick_t typedef and AON_RTC_ROUND_TICK
 * width in the authority <driver/aon_rtc_types.h>, and that header is also
 * included by translation units outside the module: bk7258_wifi_adapter.c,
 * hal_port/hw_driver_shim.c, hal_port/platform_shim.c, aon_pmu authority
 * aon_pmu_driver.c, and the third_party bk_wifi_adapter.c.  All of them must agree on the tick width, the
 * alarm_info_t/alarm_node_t layout and the published prototypes. */
#include "../../../../aon_rtc/nuttx_port/include/aon_rtc_port_config.h"

/* ALIGNED 2026-09-01: authority sdkconfig.h:72 has CONFIG_WIFI4=1 (alongside
 * WIFI6=1) -- it advertises both HT and HE.  We had 0, which zeroed the
 * erp/ht/vht fields of ME_CONFIG_REQ (rw_msg_tx.c:279-300) that the closed
 * library uses to build its rate set.  Dependency checked: the 5GHz band
 * pointer those lines dereference is populated unconditionally at
 * rw_ieee80211.c:440. */
#define CONFIG_WIFI4                1

#define CONFIG_WIFI6_IP_DEBUG       1
#define CONFIG_WIFI_BAND_5G         0
#define CONFIG_BLUETOOTH            0

/* NOT DEFINED, DELIBERATELY (2026-09-01): absent from the authority's
 * sdkconfig.h, and 3 of its test sites use `#ifdef` (wifi_v2.c:187,961,970),
 * so `#define ... 0` was switching ON WAPI code the authority never builds.
 *
 * #define CONFIG_WAPI_SUPPORT      0   <-- must stay commented out
 */

/* STA MVP keeps these off. */
/* DELIBERATE DIVERGENCE (authority has 1): enabling this needs
 * controller_wifi_if.h, which is not part of this port.  Harmless as 0 because
 * its only two `#ifdef` sites (rwnx_rx.h:6,87) are commented out upstream, so
 * the 0 never switches anything on. */
#define CONFIG_WIFI_VNET_CONTROLLER 0

/* NOT DEFINED, DELIBERATELY (2026-09-01): absent from the authority's
 * sdkconfig.h, yet 215 `#ifdef` sites test it -- including wifi_v2.c:32 and
 * rwnx_rx.c:33, which pull in four wpa_supplicant P2P headers.  Those two
 * includes are the whole reason wifi_v2.c carried an 18-line include-path
 * patch; with P2P undefined the block vanishes and the patch becomes
 * unnecessary.
 *
 * #define CONFIG_P2P               0   <-- must stay commented out
 */

#define CONFIG_BRIDGE               0

/* ALIGNED 2026-09-01 (authority sdkconfig.h): both were 0 here.  Deps checked
 * before enabling --
 *   STA_AUTO_RECONNECT: wlan_sta_set_autoreconnect (wifi_v2.c:1024) and the
 *     2-arg wpa_ctrl_request (ctrl_iface.c:133, compiled via
 *     hal_port/vendor_sources/wpa_ctrl_iface.c) both resolve.
 *   WIFI_SCAN_COUNTRY_CODE: SCAN_TYPE_CC (wifi_types.h:515),
 *     rw_msg_send_scan_cancel_req (rw_msg_tx.c:1479), bk_wifi_bcn_cc_rxed_cb
 *     (wifi_v2.c:5457, itself inside this same guard) all resolve.  The added
 *     runtime path at rw_msg_rx.c:1048 is gated on `site_survey_cc`, which
 *     stays false unless a country-code scan is explicitly started. */
#define CONFIG_STA_AUTO_RECONNECT   1
#define CONFIG_MONITOR_REQ          0
#define CONFIG_WIFI_SCAN_COUNTRY_CODE 1
/* Authority BK7258 defaults 2.4 GHz country policy to automatic.  The
 * already-vendored scan builders then mark channels 12/13/14 CHAN_NO_IR until
 * a country IE establishes the regulatory domain.  The country-code scan
 * producer/consumer closure above is already enabled. */
#define CONFIG_WIFI_AUTO_COUNTRY_CODE 1
/* CONFIG_ROLE_* comes from the vendored Armino bk_wifi_types.h.  Do not
 * define it here: these are enum-like ABI values, not NuttX booleans. */

/* Debug/trace off at skeleton stage. */
#define CONFIG_RWNX_PROTO_DEBUG     0
#define CONFIG_RWNX_TD              1
#define CONFIG_RWNX_SW_TXQ          1
#define CONFIG_SPECIAL_TX_TYPE      1
#define CONFIG_WIFI_MAC_SUPPORT_STAS_MAX_NUM 2
#define CONFIG_WIFI_KMSG_TASK_PRIO  3
#define CONFIG_WIFI_KMSG_TASK_STACK_SIZE 4096
#define CONFIG_WIFI_CORE_TASK_PRIO  2
#define CONFIG_WIFI_CORE_TASK_STACK_SIZE 2048
/* Authority BK7258 enables this.  The vendored scan producer, WPA consumer
 * and hostapd_intf ownership release provider are all already in the runtime
 * source closure, so select the authority wpa_scan_res ABI and its matching
 * release discipline rather than the incompatible sta_scan_res branch. */
#define CONFIG_MINIMUM_SCAN_RESULTS 1
/* DELIBERATE DIVERGENCE (authority has 1): CONFIG_SHELL_ASYNCLOG=1 requires
 * shell_cmd_ind_out(), which this port does not provide -- enabling it would
 * not link.  Keeping 0 is safe AND already authority-equivalent at the one site
 * that matters: bk_wifi_adapter.c:1399 tests it with `#ifdef`, so our
 * `#define ... 0` selects the same `._shell_assert_out = shell_assert_out` arm
 * the authority selects.  The remaining sites (wifi_v2.c:3370,3384,3396) only
 * choose between shell_cmd_ind_out() and WIFI_LOG_RAW() for printing scan
 * results, which does not affect whether a scan works. */
#define CONFIG_SHELL_ASYNCLOG       0

/* ALIGNED 2026-09-01: authority sdkconfig.h has CONFIG_SCAN_SPEED_LEVEL=3; we
 * returned 0.  This value is handed straight to the closed library through
 * hal_port/feature_shim.c:103 (bk_feature_get_scan_speed_level), so a mismatch here
 * is a live behavioural difference on the scan path, not a build detail.
 * NOTE: the direction of this knob (higher = faster or slower dwell) is not
 * documented in any source we have; 3 is used because it is what the working
 * authority build reports, not because we know what it means. */
#define CONFIG_SCAN_SPEED_LEVEL     3
#define CONFIG_SOC_BK7258           1

/* PM module and PHY submodule identifiers are owned by the imported BK7258
 * authority sys_types.h.  Do not reproduce them as configuration macros:
 * source files that include the authority enum would otherwise have their
 * identifiers macro-expanded before the enum can be parsed. */

/****************************************************************************
 * "#define X 0" is not the same as "off"
 *
 * The vendored Armino sources test their CONFIG_* symbols in two different
 * ways, and the two disagree about what `#define X 0` means:
 *
 *              #ifdef X            #if X
 *   undefined  false               0   (warns under -Wundef, then acts as 0)
 *   0          TRUE  <-- trap      0
 *   1          true                1
 *
 * So a symbol written as `#define X 0` still switches ON every `#ifdef X`
 * block.  Where the authority's sdkconfig.h omits a symbol entirely, we must
 * omit it too -- writing 0 compiles vendored code the authority never builds.
 * That is why CONFIG_P2P, CONFIG_WAPI_SUPPORT and CONFIG_FREERTOS_SMP appear
 * above only as commented-out reminders.
 *
 * -Wundef is enabled for this tree (nuttx/CMakeLists.txt:622) but -Werror is
 * not, so undefined symbols in `#if` sites produce warnings and evaluate to 0.
 * The warnings are the expected cost of matching the authority; do not silence
 * them by reintroducing `#define ... 0`.
 *
 * When changing anything here, classify the symbol first:
 *   1. authority omits it  -> omit it (comment out, with the reason)
 *   2. authority sets N    -> set N, after checking the dependencies of every
 *                             block it switches on actually resolve here
 *   3. authority sets N but this port cannot support it -> keep our value and
 *                             say why, in a DELIBERATE DIVERGENCE comment
 ****************************************************************************/

#endif /* __BK7258_WIFI_GLUE_COMMON_SYS_CONFIG_H */

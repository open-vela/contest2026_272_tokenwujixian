/*
 * chips/bk7258/wifi/hal_port/feature_shim.c
 *
 * Feature-query predicates declared in hal_port/include/bk_feature.h. Upstream
 * these live in the bk_common component (bk_common/bk_feature.c), which we do
 * not vendor.
 *
 * bk_wifi_adapter.c wraps 14 of these into g_wifi_funcs, so libwifi.a queries
 * them at runtime to pick behaviour. Each one therefore reproduces upstream's
 * exact condition instead of returning a hardcoded value -- that way the
 * answers stay derived from common/sys_config.h, and flipping a config there
 * cannot silently disagree with what the vendor runtime is told.
 *
 * Tests are written `#if defined(X) && X` rather than `#if X`: our convention
 * is that a disabled feature is left undefined (see sys_config.h), and the
 * build runs with -Wundef.
 */

#include <nuttx/config.h>

#include <common/bk_include.h>

#include "bk_feature.h"

/****************************************************************************
 * Connection behaviour
 ****************************************************************************/

int bk_feature_bssid_connect_enable(void)
{
#if defined(CONFIG_BSSID_CONNECT) && CONFIG_BSSID_CONNECT
  return 1;
#else
  return 0;
#endif
}

int bk_feature_fast_connect_enable(void)
{
#if defined(CONFIG_WIFI_FAST_CONNECT) && CONFIG_WIFI_FAST_CONNECT
  return 1;
#else
  return 0;
#endif
}

int bk_feature_fast_dhcp_enable(void)
{
  /* Off by design: the DHCP-done bridge is driven by the NuttX lease, so the
   * vendor fast-DHCP path must not run (plan §11.4.2).
   */

#if defined(CONFIG_WIFI_FAST_DHCP) && CONFIG_WIFI_FAST_DHCP
  return 1;
#else
  return 0;
#endif
}

int bk_feature_send_deauth_before_connect(void)
{
#if defined(CONFIG_DEAUTH_BEFORE_CONNECT) && CONFIG_DEAUTH_BEFORE_CONNECT
  return 1;
#else
  return 0;
#endif
}

int bk_feature_not_check_ssid_enable(void)
{
#if defined(CONFIG_NOT_CHECK_SSID_CHANGE) && CONFIG_NOT_CHECK_SSID_CHANGE
  return 1;
#else
  return 0;
#endif
}

int bk_feature_sta_vsie_enable(void)
{
#if defined(CONFIG_COMPONENTS_STA_VSIE) && CONFIG_COMPONENTS_STA_VSIE
  return 1;
#else
  return 0;
#endif
}

int bk_feature_network_found_event(void)
{
#if defined(CONFIG_NETWORK_FOUND_EVENT_ENABLE) && \
    CONFIG_NETWORK_FOUND_EVENT_ENABLE
  return 1;
#else
  return 0;
#endif
}

/****************************************************************************
 * Scan / station table
 ****************************************************************************/

int bk_feature_get_scan_speed_level(void)
{
#if defined(CONFIG_SCAN_SPEED_LEVEL)
  return CONFIG_SCAN_SPEED_LEVEL;
#else
  return 0;
#endif
}

int bk_feature_get_mac_sup_sta_max_num(void)
{
#if defined(CONFIG_WIFI_MAC_SUPPORT_STAS_MAX_NUM) && \
    CONFIG_WIFI_MAC_SUPPORT_STAS_MAX_NUM
  return CONFIG_WIFI_MAC_SUPPORT_STAS_MAX_NUM;
#else
  return 2;
#endif
}

int bk_feature_ap_statype_limit_enable(void)
{
#if defined(CONFIG_AP_STATYPE_LIMIT) && CONFIG_AP_STATYPE_LIMIT
  return 1;
#else
  return 0;
#endif
}

int bk_feature_receive_bcmc_enable(void)
{
#if defined(CONFIG_RECEIVE_BCMC_IN_DTIM10) && CONFIG_RECEIVE_BCMC_IN_DTIM10
  return 1;
#else
  return 0;
#endif
}

/****************************************************************************
 * Platform / PHY
 ****************************************************************************/

int bk_feature_get_cpu_cnt(void)
{
#if defined(CONFIG_CPU_CNT) && CONFIG_CPU_CNT
  return CONFIG_CPU_CNT;
#else
  return 1;
#endif
}

int bk_feature_config_cache_enable(void)
{
  /* Off until cache attributes for the shared MAC buffers are established --
   * see the sys_config.h deviation list.
   */

#if defined(CONFIG_CACHE_ENABLE) && CONFIG_CACHE_ENABLE
  return 1;
#else
  return 0;
#endif
}

int bk_feature_ckmn_enable(void)
{
#if defined(CONFIG_CKMN) && CONFIG_CKMN
  return 1;
#else
  return 0;
#endif
}

int bk_feature_temp_detect_enable(void)
{
#if defined(CONFIG_TEMP_DETECT) && CONFIG_TEMP_DETECT
  return 1;
#else
  return 0;
#endif
}

int bk_feature_close_coexist_csa(void)
{
#if defined(CONFIG_CLOSE_COEXIST_CSA) && CONFIG_CLOSE_COEXIST_CSA
  return 1;
#else
  return 0;
#endif
}

int bk_feature_save_rfcali_to_otp_enable(void)
{
  /* Upstream requires both OTP support and the RF-cali-to-OTP option. OTP is
   * not ported for BK7258, so this stays 0 and RF calibration data is not
   * persisted.
   */

#if defined(CONFIG_OTP_V1) && CONFIG_OTP_V1 && \
    defined(CONFIG_PHY_RFCALI_TO_OTP) && CONFIG_PHY_RFCALI_TO_OTP
  return 1;
#else
  return 0;
#endif
}

int bk_feature_phy_log_enable(void)
{
#if defined(CONFIG_PHY_LOG_ENABLE) && CONFIG_PHY_LOG_ENABLE
  return 1;
#else
  return 0;
#endif
}

int bk_feature_wifi_signal_cert_enable(void)
{
#if defined(CONFIG_WIFI_SIGNAL_CERT) && CONFIG_WIFI_SIGNAL_CERT
  return 1;
#else
  return 0;
#endif
}

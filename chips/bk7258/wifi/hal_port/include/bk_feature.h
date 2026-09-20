/*
 * chips/bk7258/wifi/hal_port/include/bk_feature.h
 *
 * Feature-query predicates. Upstream these live in the bk_common component,
 * which we do not vendor, so both the declarations and the implementations
 * (hal_port/feature_shim.c) are ours.
 *
 * These are not cosmetic: bk_wifi_adapter.c wraps 14 of them into
 * g_wifi_funcs, so libwifi.a queries them at runtime to decide behaviour
 * (fast connect, BSSID-pinned connect, station-table size, scan speed, ...).
 * The shim therefore reproduces upstream's exact conditions rather than
 * returning fixed values, so the answers track common/sys_config.h.
 */

#ifndef __BK7258_WIFI_GLUE_BK_FEATURE_H
#define __BK7258_WIFI_GLUE_BK_FEATURE_H

#ifdef __cplusplus
extern "C" {
#endif

int bk_feature_bssid_connect_enable(void);
int bk_feature_fast_connect_enable(void);
int bk_feature_fast_dhcp_enable(void);
int bk_feature_sta_vsie_enable(void);
int bk_feature_ap_statype_limit_enable(void);
int bk_feature_temp_detect_enable(void);
int bk_feature_get_cpu_cnt(void);
int bk_feature_receive_bcmc_enable(void);
int bk_feature_not_check_ssid_enable(void);
int bk_feature_close_coexist_csa(void);
int bk_feature_get_mac_sup_sta_max_num(void);
int bk_feature_network_found_event(void);
int bk_feature_config_cache_enable(void);
int bk_feature_ckmn_enable(void);
int bk_feature_send_deauth_before_connect(void);
int bk_feature_get_scan_speed_level(void);
int bk_feature_save_rfcali_to_otp_enable(void);
int bk_feature_phy_log_enable(void);
int bk_feature_wifi_signal_cert_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BK_FEATURE_H */

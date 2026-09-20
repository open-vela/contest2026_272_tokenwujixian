/*
 * chips/bk7258/wifi/hal_port/include/bluetooth_internal.h
 *
 * Bluetooth-side hooks the vendored bk_phy_adapter.c calls for Wi-Fi/BT
 * coexistence: BT transmit-power lookup, the two RF-path power setters, and the
 * RF test-mode retrigger. All four sit behind `#if CONFIG_BLUETOOTH`
 * (bk_phy_adapter.c:299-335), which is 1 in the prebuilt baseline -- the PHY is
 * shared between Wi-Fi and BT, so the coexistence paths inside libbk_phy.a
 * expect that.
 *
 * Signatures are upstream's (bk_bluetooth/include/private/bluetooth_internal.h
 * :19-29). Note the empty parameter lists are upstream's too, not oversights.
 *
 * libbk_phy.a neither defines nor references these (checked with nm: absent from
 * both the defined and the undefined lists), so the symbols have to come from
 * us -- see hal_port/bt_coex_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_BLUETOOTH_INTERNAL_H
#define __BK7258_WIFI_GLUE_BLUETOOTH_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t get_tx_pwr_idx(void);
void    txpwr_max_set_bt_polar(void);
void    txpwr_max_set_bt_iq(void);
void    bluetooth_rf_test_mode_retrig(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BLUETOOTH_INTERNAL_H */

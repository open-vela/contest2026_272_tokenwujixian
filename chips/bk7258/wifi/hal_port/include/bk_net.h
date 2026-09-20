/*
 * chips/bk7258/wifi/hal_port/include/bk_net.h
 *
 * Upstream this header belongs to the bk_netif component, which is dropped in
 * favour of the NuttX network stack (plan §11.4.6). It declares exactly one
 * function, and bk_wifi_adapter.c:189 wraps it into g_wifi_funcs so that
 * libwifi.a can register an interface.
 *
 * The signature is upstream's, `void *mac` -- deliberately different from the
 * `uint8_t *mac` of net_wlan_remove_netif() in the port/net.h bridge, because
 * that asymmetry exists upstream too. The implementation lives with the rest
 * of the network bridge.
 */

#ifndef __BK7258_WIFI_GLUE_BK_NET_H
#define __BK7258_WIFI_GLUE_BK_NET_H

#ifdef __cplusplus
extern "C" {
#endif

int net_wlan_add_netif(void *mac);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BK_NET_H */

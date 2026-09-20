/*
 * chips/bk7258/wifi/hal_port/include/lwip_intf_v2_1/lwip-2.1.2/port/net.h
 *
 * Network bridge declarations (plan §11.4.2). The vendored bk_wifi_adapter.c
 * reaches this header through a component-relative include
 * (`#include <../../lwip_intf_v2_1/lwip-2.1.2/port/net.h>`, line 22), so the
 * path is dictated by upstream; see ../../../_relpath_anchor/README.md for how
 * it resolves here instead of into a vendored lwIP tree.
 *
 * Upstream these are lwIP netif lifecycle calls. We keep the signatures and
 * re-point the implementations at NuttX netdev: bk_wifi_adapter.c wraps them
 * into g_wifi_funcs (lines 169-194), so libwifi.a drives interface up/down
 * through here.
 *
 * Only the subset the compiled glue calls is declared. P2P / bridge / ethernet
 * variants stay out of the STA MVP scope.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_PORT_NET_H
#define __BK7258_WIFI_GLUE_LWIP_PORT_NET_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wlan_ip_config;

/* STA / AP interface lifecycle. */

void     sta_ip_start(void);
void     sta_ip_down(void);
void     uap_ip_start(void);
void     uap_ip_down(void);
uint32_t sta_ip_is_start(void);
uint32_t uap_ip_is_start(void);
void     sta_ip_mode_set(int dhcp);

/* netif registration, keyed by MAC.
 *
 * net_wlan_add_netif() is intentionally NOT declared here: upstream puts it in
 * bk_net.h with a `void *mac` parameter, and bk_wifi_adapter.c includes both
 * headers. Declaring it here as `uint8_t *` collided in every such TU.
 */

int   net_wlan_remove_netif(uint8_t *mac);
void *net_get_sta_handle(void);
void *net_get_uap_handle(void);

/* Address query / configuration. */

int  net_get_if_macaddr(void *macaddr, void *intrfc_handle);
int  net_get_if_addr(struct wlan_ip_config *addr, void *intrfc_handle);
void ip_address_set(int iface, int dhcp, char *ip, char *mask, char *gw,
                    char *dns);
void net_restart_dhcp(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_LWIP_PORT_NET_H */

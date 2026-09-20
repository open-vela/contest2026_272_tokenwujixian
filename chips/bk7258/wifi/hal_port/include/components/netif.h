#ifndef __BK7258_WIFI_GLUE_COMPONENTS_NETIF_H
#define __BK7258_WIFI_GLUE_COMPONENTS_NETIF_H
#include <common/bk_err.h>
#include <components/netif_types.h>
bk_err_t bk_netif_init(void);
bk_err_t bk_netif_set_ip4_config(netif_if_t ifx, const netif_ip4_config_t *config);
bk_err_t bk_netif_get_ip4_config(netif_if_t ifx, netif_ip4_config_t *config);
bk_err_t bk_netif_get_ip6_addr_info(netif_if_t ifx);
bk_err_t bk_netif_dhcpc_start(netif_if_t ifx);
bk_err_t bk_netif_static_ip(netif_ip4_config_t static_ip4_config);
#endif

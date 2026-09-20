/*
 * chips/bk7258/wifi/hal_port/include/lwip/netif.h
 *
 * Minimal `struct netif` for the vendored glue. Part of the network bridge
 * (plan §11.4.2): the lwIP netif layer is replaced by NuttX netdev, but the
 * glue still passes an opaque netif pointer through g_wifi_funcs and reads one
 * field from it (bk_wifi_adapter.c:138 `((struct netif *)netif)->hostname`).
 *
 * Only the fields the compiled glue actually touches are declared. This is a
 * bridge type, not lwIP's 679-line netif.h: no netif_add/netif_input/DHCP.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_NETIF_H
#define __BK7258_WIFI_GLUE_LWIP_NETIF_H

#include "lwip/arch.h"
#include "lwip/err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct netif
{
  void       *state;    /* Driver private pointer (our bk7258_wifi_s) */
  const char *hostname; /* NULL is valid */
  u8_t        num;      /* Interface index */
};

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_LWIP_NETIF_H */

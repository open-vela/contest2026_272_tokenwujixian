/*
 * chips/bk7258/wifi/hal_port/include/lwip/prot/ethernet.h
 *
 * Ethernet header layout for the vendored glue (rwnx_rx.c:25 includes
 * "prot/ethernet.h" and uses struct eth_hdr, ETHTYPE_IP, ETHTYPE_ARP).
 *
 * Ours rather than vendored, for the same reason as lwip/pbuf.h: only the
 * on-the-wire layout is reused, not the lwIP stack. Upstream needs arch/cc.h
 * for its PACK_STRUCT_FLD_* macros; the layout itself is 14 fixed bytes.
 *
 * The EtherType constants live in prot/ieee.h, as upstream splits them --
 * rwnx_tx.c includes that header on its own, so defining them in both places
 * would collide in any file reaching both.
 *
 * ETH_PAD_SIZE is 0 here (as in upstream opt.h:692), so SIZEOF_ETH_HDR is 14
 * and there is no leading padding field.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_PROT_ETHERNET_H
#define __BK7258_WIFI_GLUE_LWIP_PROT_ETHERNET_H

#include "lwip/arch.h"
#include "lwip/prot/ieee.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ETH_HWADDR_LEN   6
#define ETH_PAD_SIZE     0
#define SIZEOF_ETH_HDR   (14 + ETH_PAD_SIZE)

/** An Ethernet MAC address */

struct eth_addr
{
  u8_t addr[ETH_HWADDR_LEN];
} __attribute__((packed));

#define ETH_ADDR(b0, b1, b2, b3, b4, b5) {{b0, b1, b2, b3, b4, b5}}

/** Ethernet header. Field names match upstream: the glue dereferences
 * ->dest / ->src / ->type.
 */

struct eth_hdr
{
  struct eth_addr dest;
  struct eth_addr src;
  u16_t           type;
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_LWIP_PROT_ETHERNET_H */

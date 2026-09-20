/*
 * chips/bk7258/wifi/hal_port/include/lwip/prot/ieee.h
 *
 * EtherType values, split out from prot/ethernet.h exactly as upstream does
 * (rwnx_tx.c includes "prot/ieee.h" directly for ETHTYPE_IP / ETHTYPE_ARP /
 * ETHTYPE_RARP / ETHTYPE_IPV6 / ETHTYPE_EAPOL).
 *
 * Ours, not vendored: only the on-the-wire constants are reused, not the lwIP
 * stack (plan §11.4.6). Values are upstream's.
 *
 * ETHTYPE_EAPOL (0x888e) is the one the network bridge must demux before
 * handing frames to the NuttX IP path -- see plan §11.4.2.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_PROT_IEEE_H
#define __BK7258_WIFI_GLUE_LWIP_PROT_IEEE_H

#ifdef __cplusplus
extern "C" {
#endif

enum lwip_ieee_eth_type
{
  ETHTYPE_IP        = 0x0800u,
  ETHTYPE_ARP       = 0x0806u,
  ETHTYPE_WOL       = 0x0842u,
  ETHTYPE_RARP      = 0x8035u,
  ETHTYPE_VLAN      = 0x8100u,
  ETHTYPE_IPV6      = 0x86ddu,
  ETHTYPE_PPPOEDISC = 0x8863u,
  ETHTYPE_PPPOE     = 0x8864u,
  ETHTYPE_EAPOL     = 0x888eu,
  ETHTYPE_PROFINET  = 0x8892u,
  ETHTYPE_ETHERCAT  = 0x88a4u,
  ETHTYPE_LLDP      = 0x88ccu,
  ETHTYPE_SERCOS    = 0x88cdu,
  ETHTYPE_MRP       = 0x88e3u,
  ETHTYPE_PTP       = 0x88f7u,
  ETHTYPE_QINQ      = 0x9100u
};

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_LWIP_PROT_IEEE_H */

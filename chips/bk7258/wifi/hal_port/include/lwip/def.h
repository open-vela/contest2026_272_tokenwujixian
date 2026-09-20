/*
 * chips/bk7258/wifi/hal_port/include/lwip/def.h
 *
 * Byte-order helpers and min/max macros the vendored glue takes from lwIP
 * (htons is used 7x across rwnx_rx.c / rwnx_tx.c / bk_wifi_adapter.c, and
 * wpa_supplicant's common.h includes this header in its BK_SUPPLICANT branch).
 *
 * Backed by the NuttX network byte-order functions rather than lwIP's own
 * implementation: only the pbuf layout is reused from lwIP, not its code
 * (plan §11.4.6). Upstream's def.h also pulls in arch/perf.h, which Armino
 * does not even ship.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_DEF_H
#define __BK7258_WIFI_GLUE_LWIP_DEF_H

#include <arpa/inet.h>

#include "lwip/arch.h"

#define LWIP_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define LWIP_MIN(x, y) (((x) < (y)) ? (x) : (y))

/* lwIP exposes both the standard names and lwip_-prefixed aliases; the glue
 * uses htons() and lwip_htons() interchangeably.
 */

#define lwip_htons(x) htons(x)
#define lwip_ntohs(x) ntohs(x)
#define lwip_htonl(x) htonl(x)
#define lwip_ntohl(x) ntohl(x)

#define PP_HTONS(x) ((u16_t)((((x) & 0x00ffU) << 8) | (((x) & 0xff00U) >> 8)))
#define PP_NTOHS(x) PP_HTONS(x)
#define PP_HTONL(x) ((u32_t)((((x) & 0x000000ffUL) << 24) | \
                             (((x) & 0x0000ff00UL) <<  8) | \
                             (((x) & 0x00ff0000UL) >>  8) | \
                             (((x) & 0xff000000UL) >> 24)))
#define PP_NTOHL(x) PP_HTONL(x)

#endif /* __BK7258_WIFI_GLUE_LWIP_DEF_H */

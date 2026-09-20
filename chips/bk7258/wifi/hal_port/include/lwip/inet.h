/*
 * chips/bk7258/wifi/hal_port/include/lwip/inet.h
 *
 * Address helpers for the vendored glue, backed by the NuttX network stack
 * rather than lwIP (plan §11.4.6).
 *
 * inet_ntoa must stay a *macro* here, because upstream's is one and the glue
 * depends on its side effect. Upstream lwIP has
 *
 *     #define inet_ntoa(addr)  ip4addr_ntoa((const ip4_addr_t *)&(addr))
 *
 * and bk_wifi_adapter.c:152-155 calls it as
 *
 *     void *bk_inet_ntoa_wrapper(void *addr) { return inet_ntoa(addr); }
 *
 * so `&(addr)` takes the address of the *parameter slot*: the pointer's own four
 * bytes are read as the IPv4 address. The value passed through the vendor
 * function table is therefore an address-as-integer, not a pointer to one.
 *
 * NuttX's inet_ntoa() is a real function taking `struct in_addr` by value, so
 * simply letting the call resolve to it fails to compile ("incompatible type
 * for argument 1") -- and casting the argument to silence that would read the
 * wrong four bytes. Hence the macro below reproduces upstream's address-of
 * behaviour and routes to a helper of our own.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_INET_H
#define __BK7258_WIFI_GLUE_LWIP_INET_H

#include <netinet/in.h>
#include <arpa/inet.h>

#include "lwip/arch.h"

/* lwIP spells its address type ip_addr_t; the glue only ever passes it around
 * as an opaque 4-byte IPv4 address (bk_wifi_adapter.c:204).
 */

typedef struct
{
  u32_t addr;
} ip_addr_t;

typedef ip_addr_t ip4_addr_t;

/* Formats the four bytes at *addr as a dotted quad, into a static buffer --
 * matching upstream ip4addr_ntoa(), including its non-reentrancy.
 * Implemented in hal_port/netif_shim.c.
 */

char *bk7258_ip4addr_ntoa(const void *addr);

/* Shadow NuttX's function-form inet_ntoa for vendored translation units only.
 * The declaration above has already been parsed by this point, so libc keeps
 * working for anything that calls it before this header.
 */

#undef inet_ntoa
#define inet_ntoa(addr) bk7258_ip4addr_ntoa((const void *)&(addr))

#endif /* __BK7258_WIFI_GLUE_LWIP_INET_H */

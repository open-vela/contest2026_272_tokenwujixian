/*
 * chips/bk7258/wifi/hal_port/include/wpa_compat/ip_addr.h
 *
 * Replaces wpa_supplicant's src/utils/ip_addr.h, which defines its own
 * `struct in_addr` (a bare `int s_addr`) and `struct in6_addr` because the
 * Armino WPA port had no real IP stack. Both collide with the NuttX socket
 * layer ("redefinition of 'struct in_addr'"), reached here via
 * wifi_v2.c:35 -> wpa_supplicant_i.h:23 -> "ip_addr.h".
 *
 * This file wins over the vendored one because a quoted include falls back to
 * the -I list (ip_addr.h does not sit next to wpa_supplicant_i.h), and
 * wpa_compat precedes wpa_supplicant/src/utils there.
 *
 * Note the layouts are NOT identical: NuttX's in6_addr exposes s6_addr[16]
 * where upstream had a `.un.u32_addr/.un.u8_addr` union. Nothing in the
 * compiled set touches those members; a future file that does will need a
 * conversion rather than a silent recompile.
 */

#ifndef IP_ADDR_H
#define IP_ADDR_H

#include <netinet/in.h>

struct hostapd_ip_addr
{
  int af; /* AF_INET / AF_INET6 */
  union
  {
    struct in_addr  v4;
#ifdef CONFIG_IPV6
    struct in6_addr v6;
#endif
    u8              max_len[16];
  } u;
};

const char *hostapd_ip_txt(const struct hostapd_ip_addr *addr, char *buf,
                           size_t buflen);
int hostapd_parse_ip_addr(const char *txt, struct hostapd_ip_addr *addr);

#endif /* IP_ADDR_H */

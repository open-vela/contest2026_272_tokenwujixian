/*
 * chips/bk7258/wifi/hal_port/include/lwip/sockets.h
 *
 * Socket API for the vendored glue, taken from the NuttX network stack rather
 * than lwIP (plan §11.4.6). bk_wifi_adapter.c:1157-1171 opens a UDP socket and
 * sendto()s on it for the airkiss broadcast helper, using socket(), sendto(),
 * struct sockaddr_in, AF_INET, SOCK_DGRAM and IPPROTO_UDP -- all of which NuttX
 * provides.
 *
 * Including NuttX headers here is safe only because the wpa_supplicant
 * bk_patch directory is off the include path; it used to shadow libc
 * <signal.h> and break these chains. See hal_port/include/wpa_compat/README.md.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_SOCKETS_H
#define __BK7258_WIFI_GLUE_LWIP_SOCKETS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#endif /* __BK7258_WIFI_GLUE_LWIP_SOCKETS_H */

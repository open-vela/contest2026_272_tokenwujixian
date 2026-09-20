/*
 * chips/bk7258/wifi/hal_port/include/wpa_compat/sk_intf.h
 *
 * Management/L2 packet interface between the Wi-Fi glue and wpa_supplicant.
 * Replaces the vendored bk_patch/sk_intf.h rather than forwarding to it,
 * because that header includes fake_socket.h, whose `enum sock_type` and
 * `struct sockaddr` collide with the NuttX socket layer.
 *
 * Only what the compiled glue actually references is declared (verified over
 * rw_task.c / rwnx_rx.c / rwnx_tx.c / rw_msdu.c):
 *
 *   struct ke_sk_params    all four files
 *   ke_mgmt_packet_tx()    rwnx_rx.c, rwnx_tx.c, rw_msdu.c
 *   ke_mgmt_packet_rx()    rw_task.c
 *   ke_l2_packet_rx()      rw_task.c
 *
 * The socket-number helpers (mgmt/data/ioctl_get_socket_num) and the
 * SK_INTF_*_SOCKET_NUM macros are deliberately absent: nothing in the compiled
 * set uses them, and they only make sense with Armino's fake socket layer.
 * EAPOL/WAI (0x888e) demux to the WPA entity is the network bridge's job
 * (plan §11.4.2), not this header's.
 */

#ifndef __BK7258_WIFI_GLUE_WPA_COMPAT_SK_INTF_H
#define __BK7258_WIFI_GLUE_WPA_COMPAT_SK_INTF_H

#ifndef _SK_INTF_H_
#  define _SK_INTF_H_
#endif

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <wpa_compat/fake_socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Management packet TX/RX parameters. Layout must match the vendored
 * definition: the glue fills it and wpa_supplicant reads it.
 */

struct ke_sk_params
{
  unsigned char *buf;  /* Buffer pointer */
  int            len;  /* Buffer length */
  int            flag; /* VIF index / flag */
  uint32_t       freq; /* Frequency in MHz */
};

#ifndef ETH_P_PAE
#  define ETH_P_PAE 0x888e
#endif
#ifndef ETH_P_ALL
#  define ETH_P_ALL 0x0003
#endif

#define SK_INTF_MGMT_SOCKET_NUM  (PF_PACKET + SOCK_RAW + ETH_P_ALL)
#define SK_INTF_IOCTL_SOCKET_NUM (PF_INET + SOCK_DGRAM + 0)
#define SK_INTF_DATA_SOCKET_NUM  (PF_PACKET + SOCK_RAW + ETH_P_PAE)

int ke_mgmt_peek_rxed_next_payload_size(int flag);
int ke_mgmt_packet_rx(struct ke_sk_params *params);
int ke_mgmt_packet_tx(const struct ke_sk_params *params);
int ke_l2_packet_tx(const struct ke_sk_params *params);
int ke_l2_packet_rx(struct ke_sk_params *params);
int ke_data_peek_txed_next_payload_size(int flag);
int ke_data_peek_rxed_next_payload_size(int flag);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_WPA_COMPAT_SK_INTF_H */

/*
 * chips/bk7258/wifi/hal_port/include/wpa_compat/fake_socket.h
 *
 * Replaces the vendored bk_patch/fake_socket.h (rw_task.c includes it
 * directly). Upstream that header exists because Armino's WPA port had no real
 * socket layer, so it ships its own `enum sock_type` and `struct sockaddr` --
 * both of which collide with the NuttX socket layer we do have
 * ("redefinition of 'struct sockaddr'").
 *
 * Kept here: the Armino-specific socket bookkeeping types, so vendored code
 * still compiles. Dropped: everything NuttX owns (sock_type, sockaddr, and the
 * PF_ / ETH_P_ constants) -- those come from <sys/socket.h> and friends.
 *
 * Nothing in the compiled glue actually instantiates these types (checked over
 * rw_task.c / rwnx_rx.c / rwnx_tx.c / rw_msdu.c); they are declared for source
 * compatibility only.
 */

#ifndef __BK7258_WIFI_GLUE_WPA_COMPAT_FAKE_SOCKET_H
#define __BK7258_WIFI_GLUE_WPA_COMPAT_FAKE_SOCKET_H

/* Also claim the upstream guard so quoted includes from bk_patch/fake_socket.c
 * and bk_patch/sk_intf.h cannot re-enter the conflicting Armino definition. */
#ifndef _FAKE_SOCKET_H_
#  define _FAKE_SOCKET_H_
#endif

#include <stdint.h>

#include <sys/socket.h>

#include "utils/list.h"
#include "os/os.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int SOCKET;

typedef struct
{
  struct dl_list data;
  unsigned char *msg;
  int            len;
  uint32_t       extra_info;
} SOCKET_MSG;

typedef struct
{
  struct dl_list sk_tx_msg;  /* Socket tx from non-rwip */
  struct dl_list sk_rx_msg;  /* Socket recv from rwip */
  struct dl_list sk_element; /* Link into the global socket list */
  SOCKET         sk;
} BK_SOCKET;

typedef struct
{
  struct dl_list  sk_head;
  beken_mutex_t   fs_mutex;
} SOCKET_ENTITY;

BK_SOCKET *sk_get_sk_element(SOCKET sk);

/* Per-packet type descriptor, layout as upstream fake_socket.h:240-246.
 * rw_task.c:146 casts a queued message argument back to S_TYPE_PTR and reads
 * ->type / ->vif_index, so the field order matters.
 */

typedef struct socket_type_st
{
  unsigned char type;
  unsigned char vif_index;
  void         *cb;    /* Called when the packet has been transmitted */
  void         *args;  /* Argument for that callback */
} S_TYPE_ST, *S_TYPE_PTR;

struct ke_sk_params;

SOCKET fsocket_init(int af, int type, int protocol);
int fsocket_send(SOCKET sk, const unsigned char *buf, int len,
                 S_TYPE_PTR type);
int fsocket_recv(SOCKET sk, struct ke_sk_params *params);
void fsocket_close(SOCKET sk);
int ke_sk_send(SOCKET sk, const struct ke_sk_params *params);
int ke_sk_recv(SOCKET sk, struct ke_sk_params *params);
int ke_sk_recv_peek_next_payload_size(SOCKET sk);
int fsocket_peek_recv_next_payload_size(SOCKET sk);
int ke_sk_send_peek_next_payload_size(SOCKET sk);
SOCKET_ENTITY *get_fsocket_entity(void);

#ifndef SK_PRT
#  define SK_PRT(...)  do { } while (0)
#endif
#ifndef SK_WPRT
#  define SK_WPRT(...) do { } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_WPA_COMPAT_FAKE_SOCKET_H */

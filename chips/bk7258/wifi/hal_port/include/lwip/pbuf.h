/*
 * chips/bk7258/wifi/hal_port/include/lwip/pbuf.h
 *
 * The vendor packet ABI: `struct pbuf` layout plus the constants and function
 * declarations the vendored Armino glue is built against.
 *
 * This is NOT vendored lwIP. The lwIP protocol stack is dropped in favour of
 * nuttx/net (plan §11.4.6); only the packet *layout* is reused, because the
 * glue reads pbuf fields directly (->len 56x, ->flags 42x, ->payload 41x,
 * ->next 21x, ->tot_len 8x, ->ref 6x) and does pointer arithmetic on the
 * struct size (rwnx_tx.c:581 places sk_buff at `(uint8_t *)p + sizeof(struct
 * pbuf)`). Upstream's pbuf.h drags in opt.h (3541 lines) -> lwipopts.h ->
 * arch/cc.h, i.e. the whole stack configuration; none of that is needed for
 * the layout, so this header is ours and self-contained.
 *
 * libwifi.a does not reference any pbuf_* symbol (`nm -u` is clean), so this
 * is a glue-internal contract, not a closed-source ABI: it only has to be
 * self-consistent. The implementations live in hal_port/pbuf_shim.c.
 *
 * Includes no NuttX kernel header on purpose -- see components/log.h for the
 * shadowing trap. <nuttx/config.h> is pure macros and therefore safe.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_PBUF_H
#define __BK7258_WIFI_GLUE_LWIP_PBUF_H

#include <nuttx/config.h>

#include "lwip/arch.h"
#include "lwip/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Private reserve in front of the Ethernet payload (sk_buff + tx descriptor
 * + SG entries), as expected by the vendor MAC. Single-sourced from Kconfig;
 * never hardcode the sum.
 ****************************************************************************/

#ifdef CONFIG_BK7258_WIFI_MSDU_RESV_HEAD_LENGTH
#  define BK7258_MSDU_RESV_HEAD_LENGTH CONFIG_BK7258_WIFI_MSDU_RESV_HEAD_LENGTH
#else
#  define BK7258_MSDU_RESV_HEAD_LENGTH 0
#endif

#ifdef CONFIG_BK7258_WIFI_MSDU_RESV_DESC_LENGTH
#  define BK7258_MSDU_RESV_DESC_LENGTH CONFIG_BK7258_WIFI_MSDU_RESV_DESC_LENGTH
#else
#  define BK7258_MSDU_RESV_DESC_LENGTH 0
#endif

#define PBUF_LINK_ENCAPSULATION_HLEN \
  (BK7258_MSDU_RESV_HEAD_LENGTH + BK7258_MSDU_RESV_DESC_LENGTH)

#define PBUF_LINK_HLEN      14
#define PBUF_IP_HLEN        20
#define PBUF_TRANSPORT_HLEN 20

#define PBUF_POOL_BUFSIZE   (1580 + PBUF_LINK_ENCAPSULATION_HLEN)

/****************************************************************************
 * Layer: the enum value IS the headroom reserved in front of the payload.
 ****************************************************************************/

typedef enum
{
  PBUF_TRANSPORT = PBUF_LINK_ENCAPSULATION_HLEN + PBUF_LINK_HLEN +
                   PBUF_IP_HLEN + PBUF_TRANSPORT_HLEN,
  PBUF_IP        = PBUF_LINK_ENCAPSULATION_HLEN + PBUF_LINK_HLEN +
                   PBUF_IP_HLEN,
  PBUF_LINK      = PBUF_LINK_ENCAPSULATION_HLEN + PBUF_LINK_HLEN,
  PBUF_RAW_TX    = PBUF_LINK_ENCAPSULATION_HLEN,
  PBUF_RAW       = 0
} pbuf_layer;

/* Type bits, values as upstream so glue comparisons keep their meaning. */

#define PBUF_TYPE_FLAG_STRUCT_DATA_CONTIGUOUS       0x80
#define PBUF_TYPE_FLAG_DATA_VOLATILE                0x40
#define PBUF_TYPE_ALLOC_SRC_MASK                    0x0f
#define PBUF_ALLOC_FLAG_RX                          0x0100
#define PBUF_ALLOC_FLAG_DATA_CONTIGUOUS             0x0200
#define PBUF_TYPE_ALLOC_SRC_MASK_STD_HEAP           0x00
#define PBUF_TYPE_ALLOC_SRC_MASK_STD_MEMP_PBUF      0x01
#define PBUF_TYPE_ALLOC_SRC_MASK_STD_MEMP_PBUF_POOL 0x02

typedef enum
{
  PBUF_RAM    = (PBUF_ALLOC_FLAG_DATA_CONTIGUOUS |
                 PBUF_TYPE_FLAG_STRUCT_DATA_CONTIGUOUS |
                 PBUF_TYPE_ALLOC_SRC_MASK_STD_HEAP),
  PBUF_RAM_RX = (PBUF_ALLOC_FLAG_RX | PBUF_RAM),
  PBUF_ROM    = PBUF_TYPE_ALLOC_SRC_MASK_STD_MEMP_PBUF,
  PBUF_REF    = (PBUF_TYPE_FLAG_DATA_VOLATILE |
                 PBUF_TYPE_ALLOC_SRC_MASK_STD_MEMP_PBUF),
  PBUF_POOL   = (PBUF_ALLOC_FLAG_RX |
                 PBUF_TYPE_FLAG_STRUCT_DATA_CONTIGUOUS |
                 PBUF_TYPE_ALLOC_SRC_MASK_STD_MEMP_PBUF_POOL)
} pbuf_type;

/* Misc flags. */

#define PBUF_FLAG_PUSH        0x01u
#define PBUF_FLAG_IS_CUSTOM   0x02u
#define PBUF_FLAG_MCASTLOOP   0x04u
#define PBUF_FLAG_LLBCAST     0x08u
#define PBUF_FLAG_LLMCAST     0x10u
#define PBUF_FLAG_TCP_FIN     0x20u
#define PBUF_FLAG_IS_EXTERNAL 0x40u

/****************************************************************************
 * Layout. Field order and widths must not change: the glue accesses these
 * directly and computes its private area from sizeof(struct pbuf).
 *
 * Upstream's optional PBUF_LIFETIME_DBG and CONFIG_BRIDGE fields are omitted:
 * both are disabled in our configuration and the glue never touches them
 * (verified by grep over the compiled file set).
 ****************************************************************************/

struct pbuf
{
  struct pbuf *next;      /* Next pbuf in the singly linked chain */
  void        *payload;   /* Pointer to the data in the buffer */
  u16_t        tot_len;   /* Length of this buffer plus all next ones */
  u16_t        len;       /* Length of this buffer */
  u8_t         type_internal; /* pbuf_type bits */
  u8_t         flags;     /* PBUF_FLAG_* */
  u8_t         ref;       /* Reference count */
  u8_t         if_idx;    /* Input netif index, for incoming packets */
};

#define pbuf_init()

#define pbuf_get_allocsrc(p)         ((p)->type_internal & \
                                      PBUF_TYPE_ALLOC_SRC_MASK)
#define pbuf_match_allocsrc(p, type) (pbuf_get_allocsrc(p) == \
                                      ((type) & PBUF_TYPE_ALLOC_SRC_MASK))
#define pbuf_match_type(p, type)     pbuf_match_allocsrc(p, type)

/****************************************************************************
 * The subset the compiled glue actually calls. pbuf_concat/pbuf_free_all are
 * omitted: their only caller is the vendored pbuf.c, which is excluded from
 * the build (it is gated on CONFIG_FULLY_HOSTED/SEMI_HOSTED and compiles
 * empty for our NO_HOSTED configuration).
 ****************************************************************************/

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type);
void         pbuf_ref(struct pbuf *p);
u8_t         pbuf_free(struct pbuf *p);
u8_t         pbuf_header(struct pbuf *p, s16_t header_size);
void         pbuf_cat(struct pbuf *head, struct pbuf *tail);
err_t        pbuf_copy(struct pbuf *p_to, const struct pbuf *p_from);
struct pbuf *pbuf_coalesce(struct pbuf *p, pbuf_layer layer);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_LWIP_PBUF_H */

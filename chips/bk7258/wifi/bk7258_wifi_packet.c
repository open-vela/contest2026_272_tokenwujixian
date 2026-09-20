/*
 * chips/bk7258/wifi/bk7258_wifi_packet.c
 *
 * Vendor packet shim.
 *
 * Provides a minimal, team-owned packet container that satisfies the Beken
 * MAC data-path contract: a pbuf-shaped header, a private reserve holding
 * sk_buff + tx descriptor + SG entries, and Ethernet payload. The lower half
 * converts NetPKT <-> bk7258_vpkt at the boundary; a NetPKT is never cast to
 * a vendor packet and vice versa.
 */

#include <nuttx/config.h>
#include <nuttx/kmalloc.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "bk7258_wifi_internal.h"
#include "os/mem.h"

/****************************************************************************
 * Private helpers
 ****************************************************************************/

static void bk7258_vpkt_init(struct bk7258_vpkt *p, uint8_t *payload,
                             uint16_t len, bool rx)
{
  p->next = NULL;
  p->payload = payload;
  p->tot_len = len;
  p->len = len;
  p->ref = 1;
  p->flags = rx ? BK7258_VPKT_FLAG_RX : 0;
  p->type_internal = 0;
}

/****************************************************************************
 * Public API
 ****************************************************************************/

struct bk7258_vpkt *bk7258_vpkt_alloc(uint16_t len, bool rx)
{
  struct bk7258_vpkt *p;
  size_t total;

  total = sizeof(*p) + BK7258_WIFI_PRIVATE_RESERVE + len;
  p = os_zalloc(total);
  if (p == NULL)
    {
      return NULL;
    }

  bk7258_vpkt_init(p, (uint8_t *)(p + 1) + BK7258_WIFI_PRIVATE_RESERVE,
                   len, rx);
  return p;
}

struct bk7258_vpkt *bk7258_vpkt_alloc_ref(void *payload, uint16_t len)
{
  struct bk7258_vpkt *p;

  p = os_zalloc(sizeof(*p));
  if (p == NULL)
    {
      return NULL;
    }

  bk7258_vpkt_init(p, payload, len, false);
  return p;
}

void bk7258_vpkt_ref(struct bk7258_vpkt *p)
{
  if (p != NULL)
    {
      p->ref++;
    }
}

void bk7258_vpkt_free(struct bk7258_vpkt *p)
{
  struct bk7258_vpkt *next;

  while (p != NULL)
    {
      next = p->next;
      p->next = NULL;
      if (p->ref > 0)
        {
          p->ref--;
        }

      if (p->ref == 0)
        {
          os_free(p);
        }

      p = next;
    }
}

int bk7258_vpkt_push(struct bk7258_vpkt *p, int16_t delta)
{
  if (p == NULL)
    {
      return -EINVAL;
    }

  /* Positive delta adds header space (payload moves toward the reserve);
   * negative delta strips a header (payload moves toward the data). */
  p->payload -= delta;
  p->len = (uint16_t)(p->len + delta);
  p->tot_len = (uint16_t)(p->tot_len + delta);
  return 0;
}

void *bk7258_vpkt_private_area(struct bk7258_vpkt *p)
{
  return p->payload - BK7258_WIFI_PRIVATE_RESERVE;
}

int bk7258_vpkt_coalesce(struct bk7258_vpkt **p)
{
  struct bk7258_vpkt *head = *p;
  struct bk7258_vpkt *flat;
  struct bk7258_vpkt *q;
  uint8_t *dst;
  uint16_t total;

  if (head == NULL || head->next == NULL)
    {
      return 0;
    }

  total = head->tot_len;
  flat = bk7258_vpkt_alloc(total, false);
  if (flat == NULL)
    {
      return -ENOMEM;
    }

  dst = flat->payload;
  for (q = head; q != NULL; q = q->next)
    {
      memcpy(dst, q->payload, q->len);
      dst += q->len;
    }

  bk7258_vpkt_free(head);
  *p = flat;
  return 0;
}

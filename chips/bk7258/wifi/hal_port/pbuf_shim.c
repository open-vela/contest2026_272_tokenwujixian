/* Armino pbuf ABI implementation backed by the NuttX kernel heap.
 * This is a vendor-packet object, never a NuttX netpkt/iob. */

#include <nuttx/kmalloc.h>
#include <errno.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "os/mem.h"

/* bk_pbuf_layer / bk_pbuf_type -- the enums the closed library speaks across
 * the funcs table.  Included so the translation below can use the named
 * constants instead of numeric literals; bare numbers are exactly how the
 * layer mapping drifted out of sync with this header. */

#include <generated/lmac_wifi_adapter.h>

/* Headroom in front of the payload.
 *
 * Upstream lwIP semantics: the pbuf_layer enum value IS the headroom, and
 * PBUF_RAW_TX already equals PBUF_LINK_ENCAPSULATION_HLEN. Adding the reserve
 * on top of the layer would therefore count it twice (PBUF_RAW_TX would get
 * 1416 bytes), which wastes ~708 bytes per TX packet on a ~195 KiB CP.
 *
 * The reserve cannot simply be dropped either, because the two allocation
 * conventions in the compiled vendor set disagree on sk_buff ownership:
 *
 *   rwnx_start_xmit()  places sk_buff at `p + sizeof(struct pbuf)` and the TX
 *                      descriptor right after it, i.e. embedded in this
 *                      headroom (rwnx_tx.c). It allocates with PBUF_RAW_TX.
 *   alloc_skb()        allocates sk_buff separately and only uses the pbuf for
 *                      payload (skbuff.c). It allocates with PBUF_RAW == 0.
 *
 * Honouring the layer alone would give the PBUF_RAW callers zero headroom, so
 * keep PBUF_LINK_ENCAPSULATION_HLEN as a floor for every layer. That is the
 * same single 708-byte private reserve bk7258_wifi_packet.c uses, so both
 * containers now describe one contract instead of two.
 */

static size_t pbuf_headroom(pbuf_layer layer)
{
  size_t headroom = (size_t)layer;

  return headroom > PBUF_LINK_ENCAPSULATION_HLEN ?
         headroom : (size_t)PBUF_LINK_ENCAPSULATION_HLEN;
}

/* Payload alignment.
 *
 * The authority sets MEM_ALIGNMENT 4 (cp/components/lwip_intf_v2_1/
 * lwip-2.1.2/port/lwipopts.h:160) and upstream pbuf_alloc() runs every
 * payload pointer through LWIP_MEM_ALIGN, so a payload there is always a
 * multiple of 4.  This shim replaces that allocator and had no equivalent:
 * the offset was sizeof(struct pbuf) + reserve, with nothing keeping either
 * term aligned.
 *
 * This is NOT the cause of the MAC reading a 2 mod 4 address.  That comes
 * from CPU2HW(p->payload + sizeof(ETH_HDR_T)) and 14 mod 4 == 2, and it is
 * identical on the authority, which also has ETH_PAD_SIZE 0 (its opt.h:692,
 * our prot/ethernet.h:30) -- ETH_PAD_SIZE is the knob that exists to absorb
 * those two bytes and neither side turns it on.
 *
 * What this removes is a silent trap.  With sizeof(struct pbuf) == 16 and
 * the reserve at 108 + 600 == 708, the two layers that have callers land on
 * 724, aligned by coincidence rather than by construction; PBUF_LINK,
 * PBUF_IP and PBUF_TRANSPORT already compute to 2 mod 4 and would hand the
 * DMA an odd address the moment they gained one.  Changing either
 * MSDU_RESV_*_LENGTH by an odd amount would do the same to the TX path
 * itself, with no diagnostic anywhere -- the descriptor probe in rwnx_tx.c
 * would just start printing aligned=0 for a different reason.
 *
 * Aligning up only ever grows the headroom, so the sk_buff and TX
 * descriptor that rwnx_start_xmit() embeds at `p + sizeof(struct pbuf)`
 * keep at least the room they have today.
 */

#define PBUF_MEM_ALIGNMENT 4u
#define PBUF_ALIGN_UP(n) \
  (((size_t)(n) + (PBUF_MEM_ALIGNMENT - 1u)) & \
   ~((size_t)PBUF_MEM_ALIGNMENT - 1u))

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type)
{
  struct pbuf *p;
  size_t reserve = pbuf_headroom(layer);
  size_t offset = PBUF_ALIGN_UP(sizeof(*p) + reserve);
  size_t total = offset + length;

  p = kmm_zalloc(total);
  if (p == NULL)
    {
      return NULL;
    }

  p->next = NULL;
  p->payload = (uint8_t *)p + offset;
  p->tot_len = length;
  p->len = length;
  p->type_internal = (u8_t)type;
  p->flags = 0;
  p->ref = 1;
  p->if_idx = 0;
  return p;
}

void pbuf_ref(struct pbuf *p)
{
  if (p != NULL && p->ref != 0xff)
    {
      p->ref++;
    }
}

u8_t pbuf_free(struct pbuf *p)
{
  u8_t count = 0;

  /* Refcount walk, aligned with lwIP's pbuf_free() (lwip-2.1.2
   * src/core/pbuf.c:767-830), which is what Armino actually links: its
   * bk_wifi/src/pbuf.c fallback is dead code here because that file is entirely
   * inside `#if (CONFIG_FULLY_HOSTED || CONFIG_SEMI_HOSTED)` and both builds are
   * CONFIG_NO_HOSTED=1.
   *
   * Two divergences were fixed, both only observable on CHAINED pbufs (single
   * pbufs behaved identically, which is why the EAPOL M2 path never showed it):
   *
   *   1. Stop the walk when the reference count does not reach zero.  lwIP only
   *      descends into p->next after freeing p, because a pbuf owns one
   *      reference on its successor.  The old loop kept walking and decremented
   *      every following pbuf's ref, so freeing a chain head that another owner
   *      still held could free the tail underneath them.
   *
   *   2. Never touch p->next unless p is actually freed.  The old code cleared
   *      it unconditionally, truncating a chain that the remaining owner still
   *      referenced -- its tot_len then described data no longer reachable.
   *
   * Entry with ref == 0 means a double free somewhere else.  lwIP asserts in
   * debug and, with assertions compiled out, wraps the counter and leaks.  We
   * stop instead: leaking one pbuf keeps the heap intact and leaves the real bug
   * findable, whereas the old code fell through to kmm_free() and corrupted it.
   */

  while (p != NULL)
    {
      struct pbuf *next;

      if (p->ref == 0)
        {
          break;
        }

      if (--p->ref != 0)
        {
          /* Still referenced: the rest of the chain stays owned by whoever
           * holds that reference. */

          break;
        }

      next = p->next;
      kmm_free(p);
      count++;
      p = next;
    }

  return count;
}

u8_t pbuf_header(struct pbuf *p, s16_t header_size)
{
  if (p == NULL)
    {
      return 1;
    }

  if (header_size > 0)
    {
      p->payload = (uint8_t *)p->payload - header_size;
      p->len = (u16_t)(p->len + header_size);
      p->tot_len = (u16_t)(p->tot_len + header_size);
    }
  else if (header_size < 0)
    {
      u16_t remove = (u16_t)-header_size;
      if (remove > p->len || remove > p->tot_len)
        {
          return 1;
        }
      p->payload = (uint8_t *)p->payload + remove;
      p->len = (u16_t)(p->len - remove);
      p->tot_len = (u16_t)(p->tot_len - remove);
    }
  return 0;
}

void pbuf_cat(struct pbuf *head, struct pbuf *tail)
{
  struct pbuf *p;
  if (head == NULL || tail == NULL)
    {
      return;
    }
  for (p = head; p->next != NULL; p = p->next)
    {
    }
  p->next = tail;
  head->tot_len = (u16_t)(head->len + tail->tot_len);
}

err_t pbuf_copy(struct pbuf *dst, const struct pbuf *src)
{
  const struct pbuf *s = src;
  struct pbuf *d = dst;
  u16_t remaining = dst != NULL ? dst->tot_len : 0;

  if (dst == NULL || src == NULL)
    {
      return ERR_ARG;
    }
  while (s != NULL && d != NULL && remaining != 0)
    {
      u16_t n = s->len < d->len ? s->len : d->len;
      if (n > remaining)
        {
          n = remaining;
        }
      memcpy(d->payload, s->payload, n);
      remaining -= n;
      s = s->next;
      d = d->next;
    }
  return remaining == 0 ? ERR_OK : ERR_MEM;
}

struct pbuf *pbuf_coalesce(struct pbuf *p, pbuf_layer layer)
{
  struct pbuf *flat;
  struct pbuf *q;
  uint16_t total;
  uint8_t *dst;

  if (p == NULL || p->next == NULL)
    {
      return p;
    }
  total = p->tot_len;
  flat = pbuf_alloc(layer, total, PBUF_RAM);
  if (flat == NULL)
    {
      return p;
    }
  dst = flat->payload;
  for (q = p; q != NULL; q = q->next)
    {
      memcpy(dst, q->payload, q->len);
      dst += q->len;
    }
  pbuf_free(p);
  return flat;
}

void *bk_pbuf_alloc_wrapper(int layer, uint16_t length, int type)
{
  /* The archive passes the Beken bk_pbuf_layer / bk_pbuf_type enums; our
   * pbuf_layer carries headroom byte counts and our pbuf_type carries lwIP
   * 2.1.2 flag combinations, so the values must be translated, never passed
   * through numerically.
   *
   * FIXED 2026-09-01 -- the layer switch was OFF BY ONE.  It used bare numeric
   * cases with a comment asserting "IP=0 LINK=1 RAW_TX=2 RAW=3", but
   * generated/lmac_wifi_adapter.h:26-32 actually declares
   *
   *     BK_PBUF_TRANSPORT=0  BK_PBUF_IP=1  BK_PBUF_LINK=2
   *     BK_PBUF_RAW_TX=3     BK_PBUF_RAW=4
   *
   * so every case was shifted by one and BK_PBUF_RAW (4) fell through to
   * `default: return NULL`.  The authoritative wrapper
   * (bk_wifi_adapter.c:68-76) accepts ONLY BK_PBUF_RAW_TX and BK_PBUF_RAW and
   * rejects everything else, so those two are the only values the closed
   * library ever passes: we were failing outright the allocation used for RX
   * buffers, and mis-sizing headroom on the other one.  Consistent with the
   * board accepting zero frames (isr 33 = 0 RX interrupts, sd4 bcn = 0) while
   * PHY energy measurement worked.
   *
   * The type switch was correct, but is rewritten with the same named
   * constants and narrowed to the three values the authority accepts -- bare
   * numbers are what let the layer mapping drift unnoticed. */

  pbuf_layer l;
  pbuf_type t;

  switch (layer)
    {
    case BK_PBUF_RAW_TX: l = PBUF_RAW_TX; break;
    case BK_PBUF_RAW:    l = PBUF_RAW;    break;
    default: return NULL;
    }

  switch (type)
    {
    case BK_PBUF_RAM:    t = PBUF_RAM;    break;
    case BK_PBUF_RAM_RX: t = PBUF_RAM_RX; break;
    case BK_PBUF_POOL:   t = PBUF_POOL;   break;
    default: return NULL;
    }

  return pbuf_alloc(l, length, t);
}

void bk_pbuf_free_wrapper(void *p)
{
  pbuf_free((struct pbuf *)p);
}

void bk_pbuf_ref_wrapper(void *p)
{
  pbuf_ref((struct pbuf *)p);
}

void bk_pbuf_header_wrapper(void *p, int16_t len)
{
  (void)pbuf_header((struct pbuf *)p, len);
}

void bk_pbuf_cat_wrapper(void *p, void *q)
{
  pbuf_cat((struct pbuf *)p, (struct pbuf *)q);
}

void *bk_pbuf_coalesce_wrapper(void *p)
{
  return pbuf_coalesce((struct pbuf *)p, PBUF_RAW);
}

/* These two answer questions the closed library asks BEFORE allocating, and
 * both must speak the ABI enum -- not our lwIP values.
 *
 * FIXED 2026-09-01: this returned lwIP's `PBUF_RAM_RX`, which is a bit
 * combination (0x0100|0x0200|0x80 = 896), where the slot's contract is a
 * `bk_pbuf_type` ordinal (RAM=0 RAM_RX=1 ROM=2 REF=3 POOL=4).  The library
 * feeds this answer straight back in as the `type` argument of _pbuf_alloc,
 * so 896 hit that function's `default: return NULL` -- a second independent
 * path to a failed RX buffer allocation, on top of the layer off-by-one fixed
 * above.  Authority: bk_wifi_adapter.c:118-125 returns BK_PBUF_RAM_RX when
 * MEM_TRX_DYNAMIC_EN is set, which it is in the reference build
 * (lwipopts.h:184 -> CONFIG_LWIP_MEM_TRX_DYNAMIC_EN=1, reached through
 * pbuf.h -> lwip/opt.h:51 -> lwipopts.h, so the branch is genuinely live and
 * not dead code). */

int bk_get_rx_pbuf_type_wrapper(void)
{
  return BK_PBUF_RAM_RX;
}

/* Authority returns 0 for the same MEM_TRX_DYNAMIC_EN=1 configuration
 * (bk_wifi_adapter.c:127-134): with dynamic TRX memory there is no fixed
 * PBUF_POOL, which also matches our heap-backed pbuf_alloc. */

int bk_get_pbuf_pool_size_wrapper(void)
{
  return 0;
}

/****************************************************************************
 * BK7258 Armino mailbox backend over the Vela CP/AP RPMsg link.
 *
 * Imported mailbox_channel.c and mb_ipc.c retain their channel, ACK, socket,
 * port, CRC and timeout state. This backend transports the authority four-word
 * mailbox_data_t descriptor and copies IPC payload bytes into local memory.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_MB_IPC_RPMSG

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/spinlock.h>

#include "role_config.h"
#include <common/bk_err.h>
#include <driver/mailbox_channel.h>
#include <driver/mb_ipc.h>
#include <driver/mailbox_types.h>
#include "../include/bk7258_mb_ipc.h"

#define BK_MB_RPMSG_NAME        "bk7258-mb-ipc"
#define BK_MB_RPMSG_MAGIC       UINT32_C(0x4d425250)
#define BK_MB_RPMSG_VERSION     1
#define BK_MB_RPMSG_CHUNK       384
#define BK_MB_RPMSG_MAX         4096
#define BK_MB_RPMSG_SLOTS       4
#define BK_MB_HDR_ACK_BOX       UINT32_C(0x00001000)
#define BK_MB_HDR_LOGICAL_SHIFT 24
#define BK_MB_IPC_LOGICAL       1
#define BK_MB_IPC_RESPONSE      UINT32_C(0x80)

typedef void (*bk_mb_callback_t)(mailbox_data_t *data);

struct __attribute__((packed)) bk_mb_wire_s
{
  uint32_t magic;
  uint16_t version;
  uint8_t src_cpu;
  uint8_t dst_cpu;
  uint16_t total_len;
  uint16_t offset;
  uint16_t chunk_len;
  uint16_t reserved;
  uint32_t word[4];
};

struct bk_mb_slot_s
{
  bool used;
  uint8_t src_cpu;
  uint8_t dst_cpu;
  uint8_t logical_chnl;
  uint32_t word1;
  uint16_t total_len;
  uint16_t received;
  uint32_t word[4];
  uint8_t payload[BK_MB_RPMSG_MAX];
};

struct bk_mb_state_s
{
  struct rpmsg_endpoint ept;
  bk_mb_callback_t callback[2][2];
  struct bk_mb_slot_s slot[BK_MB_RPMSG_SLOTS];
  spinlock_t lock;
  bool registered;
};

static struct bk_mb_state_s g_bk_mb =
{
  .lock = SP_UNLOCKED,
};

int bk7258_mb_ipc_initialize(void)
{
  /* mb_ipc_init owns the imported socket/router/channel state. Its first
   * mb_chnl_open reaches bk_mailbox_init(), which registers this RPMsg
   * backend against the already-created RPTUN link. */

  return mb_ipc_init();
}

_Static_assert(sizeof(mailbox_data_t) == 4 * sizeof(uint32_t),
               "authority mailbox command must remain four words");
_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t),
               "authority payload pointers use a 32-bit ABI");

static uint8_t bk_mb_local_cpu(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  return MAILBOX_CPU0;
#else
  return MAILBOX_CPU1;
#endif
}

static uint8_t bk_mb_peer_cpu(void)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  return MAILBOX_CPU1;
#else
  return MAILBOX_CPU0;
#endif
}

static bool bk_mb_is_ipc(const uint32_t word[4])
{
  return (word[0] & BK_MB_HDR_ACK_BOX) == 0 &&
         ((word[0] >> BK_MB_HDR_LOGICAL_SHIFT) & 0x0f) ==
         BK_MB_IPC_LOGICAL;
}

static bool bk_mb_role_matches(const uint32_t word[4], uint8_t src,
                               uint8_t dst)
{
  uint8_t logical_chnl = word[0] >> BK_MB_HDR_LOGICAL_SHIFT;

  return GET_SRC_CPU_ID(logical_chnl) == src &&
         GET_DST_CPU_ID(logical_chnl) == dst;
}

static struct bk_mb_slot_s *
bk_mb_find_slot(uint8_t src, uint8_t dst, uint8_t logical, uint32_t word1)
{
  unsigned int i;

  for (i = 0; i < BK_MB_RPMSG_SLOTS; i++)
    {
      struct bk_mb_slot_s *slot = &g_bk_mb.slot[i];

      if (slot->used && slot->src_cpu == src && slot->dst_cpu == dst &&
          slot->logical_chnl == logical && slot->word1 == word1)
        {
          return slot;
        }
    }

  return NULL;
}

static struct bk_mb_slot_s *
bk_mb_alloc_slot(uint8_t src, uint8_t dst, uint8_t logical, uint32_t word1)
{
  unsigned int i;

  for (i = 0; i < BK_MB_RPMSG_SLOTS; i++)
    {
      struct bk_mb_slot_s *slot = &g_bk_mb.slot[i];

      if (!slot->used)
        {
          memset(slot, 0, sizeof(*slot));
          slot->used = true;
          slot->src_cpu = src;
          slot->dst_cpu = dst;
          slot->logical_chnl = logical;
          slot->word1 = word1;
          return slot;
        }
    }

  return NULL;
}

static void bk_mb_release_response_slot(const uint32_t word[4])
{
  unsigned int i;
  uint8_t tag;
  uint8_t response_dst_port;
  uint8_t response_src_port;

  if ((word[0] & BK_MB_IPC_RESPONSE) == 0)
    {
      return;
    }

  tag = (word[1] >> 16) & 0xff;
  response_dst_port = word[1] & 0x3f;
  response_src_port = (word[1] >> 8) & 0x3f;

  for (i = 0; i < BK_MB_RPMSG_SLOTS; i++)
    {
      struct bk_mb_slot_s *slot = &g_bk_mb.slot[i];
      uint32_t request_word1 = slot->word1;

      if (slot->used && ((request_word1 >> 16) & 0xff) == tag &&
          (request_word1 & 0x3f) == response_src_port &&
          ((request_word1 >> 8) & 0x3f) == response_dst_port)
        {
          memset(slot, 0, sizeof(*slot));
          return;
        }
    }
}

static int bk_mb_receive(struct rpmsg_endpoint *ept, void *data, size_t len,
                         uint32_t source, void *priv)
{
  struct bk_mb_wire_s wire;
  struct bk_mb_slot_s *slot;
  bk_mb_callback_t callback;
  mailbox_data_t visible;
  uint32_t word[4];
  irqstate_t flags;
  uint8_t logical;
  bool ipc;

  (void)ept;
  (void)source;
  (void)priv;

  if (len < sizeof(wire))
    {
      return -EINVAL;
    }

  memcpy(&wire, data, sizeof(wire));
  memcpy(word, wire.word, sizeof(word));
  if (wire.magic != BK_MB_RPMSG_MAGIC ||
      wire.version != BK_MB_RPMSG_VERSION ||
      wire.src_cpu != bk_mb_peer_cpu() ||
      wire.dst_cpu != bk_mb_local_cpu() ||
      wire.total_len > BK_MB_RPMSG_MAX ||
      wire.chunk_len > BK_MB_RPMSG_CHUNK ||
      wire.offset > wire.total_len ||
      wire.chunk_len > wire.total_len - wire.offset ||
      len != sizeof(wire) + wire.chunk_len ||
      !bk_mb_role_matches(word, wire.src_cpu, wire.dst_cpu))
    {
      return -EINVAL;
    }

  ipc = bk_mb_is_ipc(word);
  if (!ipc && (wire.total_len != 0 || wire.offset != 0 ||
               wire.chunk_len != 0))
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bk_mb.lock);
  callback = g_bk_mb.callback[wire.src_cpu][wire.dst_cpu];
  if (callback == NULL)
    {
      spin_unlock_irqrestore(&g_bk_mb.lock, flags);
      return -ENOTCONN;
    }

  if (!ipc || wire.total_len == 0)
    {
      memcpy(&visible, word, sizeof(visible));
      spin_unlock_irqrestore(&g_bk_mb.lock, flags);
      callback(&visible);
      return 0;
    }

  logical = word[0] >> BK_MB_HDR_LOGICAL_SHIFT;
  slot = bk_mb_find_slot(wire.src_cpu, wire.dst_cpu, logical, word[1]);
  if (wire.offset == 0)
    {
      if (slot != NULL)
        {
          memset(slot, 0, sizeof(*slot));
        }

      slot = bk_mb_alloc_slot(wire.src_cpu, wire.dst_cpu, logical,
                              word[1]);
      if (slot != NULL)
        {
          slot->total_len = wire.total_len;
          memcpy(slot->word, word, sizeof(slot->word));
        }
    }

  if (slot == NULL || slot->total_len != wire.total_len ||
      slot->received != wire.offset ||
      memcmp(slot->word, word, sizeof(slot->word)) != 0)
    {
      if (slot != NULL)
        {
          memset(slot, 0, sizeof(*slot));
        }

      spin_unlock_irqrestore(&g_bk_mb.lock, flags);
      return -EPROTO;
    }

  memcpy(slot->payload + wire.offset, (const uint8_t *)data + sizeof(wire),
         wire.chunk_len);
  slot->received += wire.chunk_len;
  if (slot->received != slot->total_len)
    {
      spin_unlock_irqrestore(&g_bk_mb.lock, flags);
      return 0;
    }

  memcpy(&visible, slot->word, sizeof(visible));
  visible.param3 = (uint32_t)(uintptr_t)slot->payload;
  spin_unlock_irqrestore(&g_bk_mb.lock, flags);
  callback(&visible);
  return 0;
}

static void bk_mb_created(struct rpmsg_device *rdev, void *priv)
{
  struct bk_mb_state_s *state = priv;
  int ret;

  if (strcmp(rpmsg_get_cpuname(rdev),
#ifdef CONFIG_BK7258_COMPONENT_CP
             "ap"
#else
             "cp"
#endif
             ) != 0 || state->ept.priv != NULL)
    {
      return;
    }

  state->ept.priv = state;
  ret = rpmsg_create_ept(&state->ept, rdev, BK_MB_RPMSG_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, bk_mb_receive, NULL);
  if (ret < 0)
    {
      state->ept.priv = NULL;
    }
}

static void bk_mb_destroyed(struct rpmsg_device *rdev, void *priv)
{
  struct bk_mb_state_s *state = priv;

  if (state->ept.priv != NULL &&
      strcmp(rpmsg_get_cpuname(rdev),
#ifdef CONFIG_BK7258_COMPONENT_CP
             "ap"
#else
             "cp"
#endif
             ) == 0)
    {
      rpmsg_destroy_ept(&state->ept);
      state->ept.priv = NULL;
    }
}

bk_err_t bk_mailbox_init(void)
{
  int ret;

  if (g_bk_mb.registered)
    {
      return BK_OK;
    }

  g_bk_mb.registered = true;
  ret = rpmsg_register_callback(&g_bk_mb, bk_mb_created, bk_mb_destroyed,
                                NULL, NULL);
  if (ret < 0)
    {
      g_bk_mb.registered = false;
      return BK_ERR_MAILBOX_NOT_INIT;
    }

  return BK_OK;
}

bk_err_t bk_mailbox_deinit(void)
{
  if (g_bk_mb.registered)
    {
      g_bk_mb.registered = false;
      rpmsg_unregister_callback(&g_bk_mb, bk_mb_created, bk_mb_destroyed,
                                NULL, NULL);
    }

  return BK_OK;
}

bk_err_t bk_mailbox_set_param(mailbox_data_t *data, uint32_t p0,
                              uint32_t p1, uint32_t p2, uint32_t p3)
{
  if (data == NULL)
    {
      return BK_ERR_PARAM;
    }

  data->param0 = p0;
  data->param1 = p1;
  data->param2 = p2;
  data->param3 = p3;
  return BK_OK;
}

bk_err_t bk_mailbox_recv_callback_register(mailbox_endpoint_t src,
                                            mailbox_endpoint_t dst,
                                            bk_mb_callback_t callback)
{
  if (src != bk_mb_peer_cpu() || dst != bk_mb_local_cpu() || callback == NULL)
    {
      return BK_ERR_MAILBOX_SRC_DST;
    }

  g_bk_mb.callback[src][dst] = callback;
  return BK_OK;
}

bk_err_t bk_mailbox_recv_callback_unregister(mailbox_endpoint_t src,
                                              mailbox_endpoint_t dst)
{
  if (src != bk_mb_peer_cpu() || dst != bk_mb_local_cpu())
    {
      return BK_ERR_MAILBOX_SRC_DST;
    }

  g_bk_mb.callback[src][dst] = NULL;
  return BK_OK;
}

bk_err_t bk_mailbox_ready(mailbox_endpoint_t src, mailbox_endpoint_t dst,
                          uint32_t box_id)
{
  (void)box_id;
  return src == bk_mb_local_cpu() && dst == bk_mb_peer_cpu() &&
         is_rpmsg_ept_ready(&g_bk_mb.ept) ?
         BK_OK : BK_ERR_MAILBOX_TIMEOUT;
}

bk_err_t bk_mailbox_send(mailbox_data_t *data, mailbox_endpoint_t src,
                         mailbox_endpoint_t dst, void *arg)
{
  struct bk_mb_wire_s wire;
  uint8_t frame[sizeof(wire) + BK_MB_RPMSG_CHUNK];
  uint32_t word[4];
  const uint8_t *payload = NULL;
  uint16_t total_len = 0;
  uint16_t offset = 0;
  bool ipc;

  (void)arg;
  if (data == NULL || src != bk_mb_local_cpu() || dst != bk_mb_peer_cpu())
    {
      return BK_ERR_MAILBOX_SRC_DST;
    }

  memcpy(word, data, sizeof(word));
  if (!bk_mb_role_matches(word, src, dst) ||
      !is_rpmsg_ept_ready(&g_bk_mb.ept))
    {
      return BK_ERR_MAILBOX_TIMEOUT;
    }

  ipc = bk_mb_is_ipc(word);
  if (ipc)
    {
      total_len = word[2] & UINT32_C(0xffff);
      payload = (const uint8_t *)(uintptr_t)word[3];
      if (total_len > BK_MB_RPMSG_MAX ||
          (total_len != 0 && payload == NULL))
        {
          return BK_ERR_PARAM;
        }
    }

  do
    {
      uint16_t chunk_len = total_len - offset;
      int ret;

      if (chunk_len > BK_MB_RPMSG_CHUNK)
        {
          chunk_len = BK_MB_RPMSG_CHUNK;
        }

      memset(&wire, 0, sizeof(wire));
      wire.magic = BK_MB_RPMSG_MAGIC;
      wire.version = BK_MB_RPMSG_VERSION;
      wire.src_cpu = src;
      wire.dst_cpu = dst;
      wire.total_len = total_len;
      wire.offset = offset;
      wire.chunk_len = chunk_len;
      memcpy(wire.word, word, sizeof(word));
      memcpy(frame, &wire, sizeof(wire));
      if (chunk_len != 0)
        {
          memcpy(frame + sizeof(wire), payload + offset, chunk_len);
        }

      ret = rpmsg_send_offchannel_raw(&g_bk_mb.ept, g_bk_mb.ept.addr,
                                      g_bk_mb.ept.dest_addr, frame,
                                      sizeof(wire) + chunk_len, false);
      if (ret < 0)
        {
          return BK_ERR_MAILBOX_TIMEOUT;
        }

      offset += chunk_len;
    }
  while (offset < total_len);

  if (ipc)
    {
      irqstate_t flags = spin_lock_irqsave(&g_bk_mb.lock);
      bk_mb_release_response_slot(word);
      spin_unlock_irqrestore(&g_bk_mb.lock, flags);
    }

  return BK_OK;
}

#endif /* CONFIG_BK7258_MB_IPC_RPMSG */

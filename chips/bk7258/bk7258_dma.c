/****************************************************************************
 * contest2026_272_tokenwujixian/chips/bk7258/bk7258_dma.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "bk7258_dma.h"
#include "bk7258_memorymap.h"
#include "irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_GDMA_CHANNELS            8
#define BK7258_GDMA_TRANSFER_MAX        65536

#define BK7258_GDMA_PRIO_MODE           (BK7258_GDMA0_BASE + 0x08)
#define BK7258_GDMA_PRIO_MODE_SOFTRESET (UINT32_C(1) << 0)
#define BK7258_GDMA_PRIO_MODE_BPS_CLK_GATE (UINT32_C(1) << 1)
#define BK7258_GDMA_SECURE_ATTR         (BK7258_GDMA0_BASE + 0x10)
#define BK7258_GDMA_PRIVILEGED_ATTR     (BK7258_GDMA0_BASE + 0x14)
#define BK7258_GDMA_SECURE_CHANNELS     UINT32_C(0x0fff)

#define BK7258_GDMA_CHANNEL_BASE(ch) \
  (BK7258_GDMA0_BASE + 0x40 + ((uintptr_t)(ch) << 6))
#define BK7258_GDMA_CONTROL(ch)         (BK7258_GDMA_CHANNEL_BASE(ch) + 0x00)
#define BK7258_GDMA_DST_ADDR(ch)        (BK7258_GDMA_CHANNEL_BASE(ch) + 0x04)
#define BK7258_GDMA_SRC_ADDR(ch)        (BK7258_GDMA_CHANNEL_BASE(ch) + 0x08)
#define BK7258_GDMA_DST_LOOP_END(ch)    (BK7258_GDMA_CHANNEL_BASE(ch) + 0x0c)
#define BK7258_GDMA_DST_LOOP_START(ch)  (BK7258_GDMA_CHANNEL_BASE(ch) + 0x10)
#define BK7258_GDMA_SRC_LOOP_END(ch)    (BK7258_GDMA_CHANNEL_BASE(ch) + 0x14)
#define BK7258_GDMA_SRC_LOOP_START(ch)  (BK7258_GDMA_CHANNEL_BASE(ch) + 0x18)
#define BK7258_GDMA_REQ_MUX(ch)         (BK7258_GDMA_CHANNEL_BASE(ch) + 0x1c)
#define BK7258_GDMA_REQ_MUX_SRC_SEC_ATTR (UINT32_C(1) << 20)
#define BK7258_GDMA_REQ_MUX_DST_SEC_ATTR (UINT32_C(1) << 21)
#define BK7258_GDMA_REQ_MUX_BUS_ERROR_IE (UINT32_C(1) << 22)
#define BK7258_GDMA_STATUS(ch)          (BK7258_GDMA_CHANNEL_BASE(ch) + 0x30)

#define BK7258_GDMA_CONTROL_ENABLE      (UINT32_C(1) << 0)
#define BK7258_GDMA_CONTROL_FINISH_IE   (UINT32_C(1) << 1)
#define BK7258_GDMA_CONTROL_SRC_WIDTH(s) ((uint32_t)(s) << 4)
#define BK7258_GDMA_CONTROL_DST_WIDTH(s) ((uint32_t)(s) << 6)
#define BK7258_GDMA_CONTROL_SRC_INC     (UINT32_C(1) << 8)
#define BK7258_GDMA_CONTROL_DST_INC     (UINT32_C(1) << 9)
#define BK7258_GDMA_CONTROL_LENGTH(n) \
  (((uint32_t)(n) - 1) << 16)

#define BK7258_GDMA_STATUS_FINISH       (UINT32_C(1) << 18)
#define BK7258_GDMA_STATUS_HALF_FINISH  (UINT32_C(1) << 19)
#define BK7258_GDMA_STATUS_BUS_ERROR    (UINT32_C(1) << 20)
#define BK7258_GDMA_STATUS_EVENTS \
  (BK7258_GDMA_STATUS_FINISH | BK7258_GDMA_STATUS_HALF_FINISH | \
   BK7258_GDMA_STATUS_BUS_ERROR)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_dma_chan_s
{
  struct bk7258_dma_config_s config;
  bk7258_dma_callback_t callback;
  FAR void *arg;
  uint32_t generation;
  uint8_t channel;
  uint32_t irq_count;
  uint32_t last_events;
  bool allocated;
  bool configured;
  bool running;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_dma_chan_s g_dma_channels[BK7258_GDMA_CHANNELS];
static spinlock_t g_dma_lock = SP_UNLOCKED;
static bool g_dma_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static BK7258_DMA_HANDLE bk7258_dma_encode_handle(
  FAR const struct bk7258_dma_chan_s *channel)
{
  return (((BK7258_DMA_HANDLE)channel->generation << 3) |
          channel->channel) + 1;
}

static bool bk7258_dma_decode_handle(BK7258_DMA_HANDLE handle,
                                     FAR unsigned int *index,
                                     FAR uint32_t *generation)
{
  uintptr_t value = handle;

  if (value == 0)
    {
      return false;
    }

  value--;
  *index = value & (BK7258_GDMA_CHANNELS - 1);
  *generation = value >> 3;
  return *generation != 0;
}

static bool bk7258_dma_handle_current(
  FAR const struct bk7258_dma_chan_s *channel, uint32_t generation)
{
  return channel->allocated && channel->generation == generation;
}

static int bk7258_dma_width_encoding(enum bk7258_dma_width_e width)
{
  switch (width)
    {
      case BK7258_DMA_WIDTH_8BIT:
        return 0;

      case BK7258_DMA_WIDTH_16BIT:
        return 1;

      case BK7258_DMA_WIDTH_32BIT:
        return 2;

      default:
        return -EINVAL;
    }
}

static int bk7258_dma_validate_config(
  FAR const struct bk7258_dma_config_s *config)
{
  int src_width;
  int dst_width;

  if (config == NULL || config->src_request != BK7258_DMA_REQ_MEMORY ||
      config->dst_request != BK7258_DMA_REQ_MEMORY || config->nbytes == 0 ||
      config->nbytes > BK7258_GDMA_TRANSFER_MAX)
    {
      return -EINVAL;
    }

  src_width = bk7258_dma_width_encoding(config->src_width);
  dst_width = bk7258_dma_width_encoding(config->dst_width);
  if (src_width < 0 || dst_width < 0 ||
      config->nbytes % config->src_width != 0 ||
      config->nbytes % config->dst_width != 0 ||
      config->src_addr % config->src_width != 0 ||
      config->dst_addr % config->dst_width != 0)
    {
      return -EINVAL;
    }

  return OK;
}

static void bk7258_dma_stop_channel(FAR struct bk7258_dma_chan_s *channel)
{
  putreg32(0, BK7258_GDMA_CONTROL(channel->channel));
  putreg32(0, BK7258_GDMA_REQ_MUX(channel->channel));
  putreg32(BK7258_GDMA_STATUS_EVENTS,
           BK7258_GDMA_STATUS(channel->channel));
}

static uint32_t bk7258_dma_request_attributes(void)
{
  uint32_t req_mux = BK7258_GDMA_REQ_MUX_BUS_ERROR_IE;

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  req_mux |= BK7258_GDMA_REQ_MUX_SRC_SEC_ATTR |
             BK7258_GDMA_REQ_MUX_DST_SEC_ATTR;
#endif

  return req_mux;
}

static int bk7258_dma_interrupt(int irq, FAR void *context, FAR void *arg)
{
  unsigned int index;

  (void)irq;
  (void)context;
  (void)arg;

  for (index = 0; index < BK7258_GDMA_CHANNELS; index++)
    {
      FAR struct bk7258_dma_chan_s *channel = &g_dma_channels[index];
      bk7258_dma_callback_t callback = NULL;
      FAR void *callback_arg = NULL;
      BK7258_DMA_HANDLE callback_handle = 0;
      uint32_t events;
      int result = OK;
      irqstate_t flags;

      events = getreg32(BK7258_GDMA_STATUS(index));
      events &= BK7258_GDMA_STATUS_EVENTS;
      if (events == 0)
        {
          continue;
        }

      /* All observed status bits are W1C, including the unsupported half
       * completion indication.  The public API deliberately has no half
       * completion callback in this first implementation.
       */

      putreg32(events, BK7258_GDMA_STATUS(index));
      flags = spin_lock_irqsave(&g_dma_lock);
      channel->irq_count++;
      channel->last_events = events;
      if ((events & (BK7258_GDMA_STATUS_FINISH |
                     BK7258_GDMA_STATUS_BUS_ERROR)) != 0 &&
          channel->allocated && channel->running)
        {
          channel->running = false;
          bk7258_dma_stop_channel(channel);
          callback = channel->callback;
          callback_arg = channel->arg;
          callback_handle = bk7258_dma_encode_handle(channel);
          channel->callback = NULL;
          channel->arg = NULL;
          if ((events & BK7258_GDMA_STATUS_BUS_ERROR) != 0)
            {
              result = -EIO;
            }
        }

      spin_unlock_irqrestore(&g_dma_lock, flags);

      /* The callback may immediately configure, restart, stop or free this
       * channel.  It must therefore run after releasing the driver lock.
       */

      if (callback != NULL)
        {
          callback(callback_handle, result, callback_arg);
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

BK7258_DMA_HANDLE bk7258_dmachannel(void)
{
  unsigned int index;
  irqstate_t flags;
  BK7258_DMA_HANDLE handle = 0;

  if (!g_dma_initialized)
    {
      return 0;
    }

  flags = spin_lock_irqsave(&g_dma_lock);
  for (index = 0; index < BK7258_GDMA_CHANNELS; index++)
    {
      FAR struct bk7258_dma_chan_s *channel = &g_dma_channels[index];

      if (!channel->allocated)
        {
          channel->generation++;
          if (channel->generation == 0)
            {
              channel->generation++;
            }

          channel->allocated = true;
          channel->configured = false;
          channel->running = false;
          channel->irq_count = 0;
          channel->last_events = 0;
          channel->callback = NULL;
          channel->arg = NULL;
          handle = bk7258_dma_encode_handle(channel);
          break;
        }
    }

  spin_unlock_irqrestore(&g_dma_lock, flags);
  return handle;
}

int bk7258_dmafree(BK7258_DMA_HANDLE handle)
{
  FAR struct bk7258_dma_chan_s *channel;
  unsigned int index;
  uint32_t generation;
  irqstate_t flags;
  int ret = OK;

  if (!bk7258_dma_decode_handle(handle, &index, &generation))
    {
      return -EINVAL;
    }

  channel = &g_dma_channels[index];
  flags = spin_lock_irqsave(&g_dma_lock);
  if (!bk7258_dma_handle_current(channel, generation))
    {
      ret = -EINVAL;
    }
  else
    {
      bk7258_dma_stop_channel(channel);
      channel->callback = NULL;
      channel->arg = NULL;
      channel->configured = false;
      channel->running = false;
      channel->allocated = false;
    }

  spin_unlock_irqrestore(&g_dma_lock, flags);
  return ret;
}

int bk7258_dmasetup(BK7258_DMA_HANDLE handle,
                    FAR const struct bk7258_dma_config_s *config)
{
  FAR struct bk7258_dma_chan_s *channel;
  unsigned int index;
  uint32_t generation;
  irqstate_t flags;
  int ret;

  if (!bk7258_dma_decode_handle(handle, &index, &generation))
    {
      return -EINVAL;
    }

  ret = bk7258_dma_validate_config(config);
  if (ret < 0)
    {
      return ret;
    }

  channel = &g_dma_channels[index];
  flags = spin_lock_irqsave(&g_dma_lock);
  if (!bk7258_dma_handle_current(channel, generation))
    {
      ret = -EINVAL;
    }
  else if (channel->running)
    {
      ret = -EBUSY;
    }
  else
    {
      channel->config = *config;
      channel->configured = true;
      ret = OK;
    }

  spin_unlock_irqrestore(&g_dma_lock, flags);
  return ret;
}

int bk7258_dmastart(BK7258_DMA_HANDLE handle,
                    bk7258_dma_callback_t callback, FAR void *arg)
{
  FAR struct bk7258_dma_chan_s *channel;
  FAR const struct bk7258_dma_config_s *config;
  unsigned int index;
  uint32_t generation;
  uint32_t control;
  uint32_t req_mux;
  irqstate_t flags;
  int src_width;
  int dst_width;
  int ret = OK;

  if (!bk7258_dma_decode_handle(handle, &index, &generation) ||
      callback == NULL)
    {
      return -EINVAL;
    }

  channel = &g_dma_channels[index];
  flags = spin_lock_irqsave(&g_dma_lock);
  if (!bk7258_dma_handle_current(channel, generation) ||
      !channel->configured)
    {
      ret = -EINVAL;
      goto out;
    }

  if (channel->running)
    {
      ret = -EBUSY;
      goto out;
    }

  config = &channel->config;
  src_width = bk7258_dma_width_encoding(config->src_width);
  dst_width = bk7258_dma_width_encoding(config->dst_width);
  if (src_width < 0 || dst_width < 0)
    {
      ret = -EINVAL;
      goto out;
    }

  control = BK7258_GDMA_CONTROL_FINISH_IE |
            BK7258_GDMA_CONTROL_SRC_WIDTH(src_width) |
            BK7258_GDMA_CONTROL_DST_WIDTH(dst_width) |
            BK7258_GDMA_CONTROL_LENGTH(config->nbytes);
  if (config->src_increment)
    {
      control |= BK7258_GDMA_CONTROL_SRC_INC;
    }

  if (config->dst_increment)
    {
      control |= BK7258_GDMA_CONTROL_DST_INC;
    }

  channel->callback = callback;
  channel->arg = arg;
  channel->irq_count = 0;
  channel->last_events = 0;
  channel->running = true;

  req_mux = bk7258_dma_request_attributes();
  bk7258_dma_stop_channel(channel);
  modifyreg32(BK7258_SYS_CPU0_INT_EN, 0, BK7258_SYS_GDMA0_INT_EN);
  up_enable_irq(BK7258_IRQ_GDMA0);
  putreg32(req_mux, BK7258_GDMA_REQ_MUX(channel->channel));
  putreg32((uint32_t)config->dst_addr,
           BK7258_GDMA_DST_ADDR(channel->channel));
  putreg32((uint32_t)config->src_addr,
           BK7258_GDMA_SRC_ADDR(channel->channel));
  putreg32((uint32_t)config->dst_addr + config->nbytes,
           BK7258_GDMA_DST_LOOP_END(channel->channel));
  putreg32((uint32_t)config->dst_addr,
           BK7258_GDMA_DST_LOOP_START(channel->channel));
  putreg32((uint32_t)config->src_addr + config->nbytes,
           BK7258_GDMA_SRC_LOOP_END(channel->channel));
  putreg32((uint32_t)config->src_addr,
           BK7258_GDMA_SRC_LOOP_START(channel->channel));
  putreg32(control | BK7258_GDMA_CONTROL_ENABLE,
           BK7258_GDMA_CONTROL(channel->channel));

out:
  spin_unlock_irqrestore(&g_dma_lock, flags);
  return ret;
}

int bk7258_dma_get_diagnostics(
  BK7258_DMA_HANDLE handle, FAR struct bk7258_dma_diagnostics_s *diagnostics)
{
  FAR struct bk7258_dma_chan_s *channel;
  unsigned int index;
  uint32_t generation;
  irqstate_t flags;
  int ret = OK;

  if (diagnostics == NULL ||
      !bk7258_dma_decode_handle(handle, &index, &generation))
    {
      return -EINVAL;
    }

  channel = &g_dma_channels[index];
  flags = spin_lock_irqsave(&g_dma_lock);
  if (!bk7258_dma_handle_current(channel, generation))
    {
      ret = -EINVAL;
    }
  else
    {
      diagnostics->prio_mode = getreg32(BK7258_GDMA_PRIO_MODE);
      diagnostics->control = getreg32(BK7258_GDMA_CONTROL(index));
      diagnostics->dst_addr = getreg32(BK7258_GDMA_DST_ADDR(index));
      diagnostics->src_addr = getreg32(BK7258_GDMA_SRC_ADDR(index));
      diagnostics->dst_loop_end = getreg32(BK7258_GDMA_DST_LOOP_END(index));
      diagnostics->dst_loop_start =
        getreg32(BK7258_GDMA_DST_LOOP_START(index));
      diagnostics->src_loop_end = getreg32(BK7258_GDMA_SRC_LOOP_END(index));
      diagnostics->src_loop_start =
        getreg32(BK7258_GDMA_SRC_LOOP_START(index));
      diagnostics->req_mux = getreg32(BK7258_GDMA_REQ_MUX(index));
      diagnostics->status = getreg32(BK7258_GDMA_STATUS(index));
      diagnostics->secure_attr = getreg32(BK7258_GDMA_SECURE_ATTR);
      diagnostics->privileged_attr = getreg32(BK7258_GDMA_PRIVILEGED_ATTR);
      diagnostics->cpu0_int_enable = getreg32(BK7258_SYS_CPU0_INT_EN);
      diagnostics->irq_count = channel->irq_count;
      diagnostics->last_events = channel->last_events;
      diagnostics->running = channel->running;
    }

  spin_unlock_irqrestore(&g_dma_lock, flags);
  return ret;
}

int bk7258_dmastop(BK7258_DMA_HANDLE handle)
{
  FAR struct bk7258_dma_chan_s *channel;
  unsigned int index;
  uint32_t generation;
  irqstate_t flags;
  int ret = OK;

  if (!bk7258_dma_decode_handle(handle, &index, &generation))
    {
      return -EINVAL;
    }

  channel = &g_dma_channels[index];
  flags = spin_lock_irqsave(&g_dma_lock);
  if (!bk7258_dma_handle_current(channel, generation))
    {
      ret = -EINVAL;
    }
  else
    {
      bk7258_dma_stop_channel(channel);
      channel->callback = NULL;
      channel->arg = NULL;
      channel->running = false;
    }

  spin_unlock_irqrestore(&g_dma_lock, flags);
  return ret;
}

void arm_dma_initialize(void)
{
  unsigned int index;
  int ret;

  up_disable_irq(BK7258_IRQ_GDMA0);
  modifyreg32(BK7258_SYS_CPU0_INT_EN, BK7258_SYS_GDMA0_INT_EN, 0);
  /* Armino clears bps_clk_gate and priority state before pulsing reset.
   * Preserve neither value from the opaque Bootloader state: a retained
   * bps_clk_gate leaves configuration registers readable but stops transfers.
   */

  putreg32(0, BK7258_GDMA_PRIO_MODE);
  putreg32(BK7258_GDMA_PRIO_MODE_SOFTRESET, BK7258_GDMA_PRIO_MODE);

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  putreg32(BK7258_GDMA_SECURE_CHANNELS, BK7258_GDMA_SECURE_ATTR);
  putreg32(BK7258_GDMA_SECURE_CHANNELS, BK7258_GDMA_PRIVILEGED_ATTR);
#endif

  for (index = 0; index < BK7258_GDMA_CHANNELS; index++)
    {
      g_dma_channels[index].channel = index;
      g_dma_channels[index].generation = 0;
      g_dma_channels[index].allocated = false;
      g_dma_channels[index].configured = false;
      g_dma_channels[index].running = false;
      g_dma_channels[index].irq_count = 0;
      g_dma_channels[index].last_events = 0;
      g_dma_channels[index].callback = NULL;
      g_dma_channels[index].arg = NULL;
      bk7258_dma_stop_channel(&g_dma_channels[index]);
    }

  ret = irq_attach(BK7258_IRQ_GDMA0, bk7258_dma_interrupt, NULL);
  if (ret == OK)
    {
      modifyreg32(BK7258_SYS_CPU0_INT_EN, 0, BK7258_SYS_GDMA0_INT_EN);
      g_dma_initialized = true;
      up_enable_irq(BK7258_IRQ_GDMA0);
    }
}

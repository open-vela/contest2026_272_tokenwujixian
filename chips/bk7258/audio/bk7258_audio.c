/****************************************************************************
 * drivers/audio/bk7258_audio.c
 *
 * BK7258 internal-DAC audio lower-half.  Composes chip-level HAL from
 * chips/bk7258/audio/ (DAC + GDMA) into a NuttX audio device registered as
 * /dev/audio/pcm0p.  Board layer is expected to have registered its Power
 * Amplifier control ops via bk7258_audio_pa_register() before playback
 * starts.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/param.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <arch/chip/bk7258_audio.h>
#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_dac.h>
#include <arch/chip/bk7258_gdma.h>
#include "bk7258_adc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AUD_INFO(fmt, ...) audinfo("BK7258: " fmt "\n", ##__VA_ARGS__)
#define AUD_ERR(fmt, ...)  auderr("BK7258 ERROR: " fmt "\n", ##__VA_ARGS__)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR const struct bk7258_audio_pa_ops *g_bk7258_pa_ops;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  bk7258_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                           FAR struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  bk7258_configure(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session,
                             FAR const struct audio_caps_s *caps);
static int  bk7258_start(FAR struct audio_lowerhalf_s *dev,
                         FAR void *session);
static int  bk7258_stop(FAR struct audio_lowerhalf_s *dev,
                        FAR void *session);
static int  bk7258_reserve(FAR struct audio_lowerhalf_s *dev,
                           FAR void **session);
static int  bk7258_release(FAR struct audio_lowerhalf_s *dev,
                           FAR void *session);
#else
static int  bk7258_configure(FAR struct audio_lowerhalf_s *dev,
                             FAR const struct audio_caps_s *caps);
static int  bk7258_start(FAR struct audio_lowerhalf_s *dev);
#  ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int  bk7258_stop(FAR struct audio_lowerhalf_s *dev);
#  endif
static int  bk7258_reserve(FAR struct audio_lowerhalf_s *dev);
static int  bk7258_release(FAR struct audio_lowerhalf_s *dev);
#endif
static int  bk7258_shutdown(FAR struct audio_lowerhalf_s *dev);
static int  bk7258_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct ap_buffer_s *apb);
static int  bk7258_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                FAR struct ap_buffer_s *apb);
static int  bk7258_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                         unsigned long arg);
static void bk7258_worker(FAR void *arg);

/* Called from GDMA IRQ; kept extern so bk7258_gdma can look it up via a
 * function pointer.
 */

void bk7258_audio_dma_cb(int ch, uint32_t event, FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_bk7258_playback_ops =
{
  bk7258_getcaps,        /* getcaps        */
  bk7258_configure,      /* configure      */
  bk7258_shutdown,       /* shutdown       */
  bk7258_start,          /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  bk7258_stop,           /* stop           */
#endif
  NULL,                  /* pause          */
  NULL,                  /* resume         */
  NULL,                  /* allocbuffer    */
  NULL,                  /* freebuffer     */
  bk7258_enqueuebuffer,  /* enqueuebuffer  */
  bk7258_cancelbuffer,   /* cancelbuffer   */
  bk7258_ioctl,          /* ioctl          */
  NULL,                  /* read           */
  NULL,                  /* write          */
  bk7258_reserve,        /* reserve        */
  bk7258_release         /* release        */
};

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

static inline int bk7258_pa_enable(void)
{
  if (g_bk7258_pa_ops != NULL && g_bk7258_pa_ops->enable != NULL)
    {
      return g_bk7258_pa_ops->enable(g_bk7258_pa_ops->arg);
    }

  return OK;
}

static inline int bk7258_pa_disable(void)
{
  if (g_bk7258_pa_ops != NULL && g_bk7258_pa_ops->disable != NULL)
    {
      return g_bk7258_pa_ops->disable(g_bk7258_pa_ops->arg);
    }

  return OK;
}

/* Map upper-half 0..100 volume to a DAC digital-gain register value.
 * The endpoints match spec §3: v=0 mutes; v=100 -> AVDK 0 dB default;
 * intermediates use a linear ramp for now, replaced by a dB LUT once
 * silicon measurement data is available.
 */

static uint8_t bk7258_volume_to_gain(uint8_t volume)
{
  if (volume == 0)
    {
      return 0;
    }

  if (volume >= 100)
    {
      return BK7258_AUDIO_MAX_DAC_GAIN;
    }

  return (uint8_t)(((uint32_t)volume * BK7258_AUDIO_MAX_DAC_GAIN) / 100u);
}

static int bk7258_apply_volume(FAR struct bk7258_audio_s *priv)
{
  if (priv->volume == 0 || priv->mute)
    {
      bk7258_dac_set_gain(0);
      return bk7258_dac_mute(true);
    }

  bk7258_dac_set_gain(bk7258_volume_to_gain(priv->volume));
  return bk7258_dac_mute(false);
}

static void bk7258_notify_dequeue(FAR struct bk7258_audio_s *priv,
                                  FAR struct ap_buffer_s *apb, int result)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, result, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, result);
#endif
}

static void bk7258_notify_complete(FAR struct bk7258_audio_s *priv)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif
}

static void bk7258_notify_underrun(FAR struct bk7258_audio_s *priv,
                                    int result)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_UNDERRUN, NULL, result,
                  NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_UNDERRUN, NULL, result);
#endif
}

/* Queue LPWORK while holding priv->lock.  Pairing the closing check and
 * work_queue() under the same lock ensures shutdown either prevents a new
 * queue operation or observes an already queued item that cancel_sync can
 * cover.  work_queue() is IRQ-safe and does not sleep.
 */

static void bk7258_schedule_worker_locked(FAR struct bk7258_audio_s *priv)
{
  if (!priv->closing)
    {
      (void)work_queue(LPWORK, &priv->work, bk7258_worker, priv, 0);
    }
}

/* Close the run gate and detach every buffer affected by an asynchronous
 * failure.  recover_after_fault distinguishes a synchronous initial-start
 * failure from a runtime fault that requires stop plus OUTPUT configure.
 * Must be called with priv->lock held.
 */

static void bk7258_fault_locked(FAR struct bk7258_audio_s *priv, int error,
                                bool recover_after_fault)
{
  FAR struct ap_buffer_s *apb;

  priv->dac_running = false;
  priv->dma_active = false;
  priv->faulted = true;
  priv->fault_pending = true;
  priv->recover_after_fault = recover_after_fault;
  priv->complete_pending = false;
  priv->underrun_pending = true;
  priv->final_seen = false;

  priv->dma_error = error;

  while ((apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq)) != NULL)
    {
      dq_addlast(&apb->dq_entry, &priv->failq);
    }
}

/* Kick off a DMA transfer for the buffer at the head of pendq.  Must be
 * called with priv->lock held.  dma_active prevents the same head from being
 * programmed twice.  Returns zero if no buffer is queued/already active or
 * the transfer starts successfully; otherwise returns a negated errno.
 */

static int bk7258_start_next_locked(FAR struct bk7258_audio_s *priv)
{
  FAR struct ap_buffer_s *apb;
  struct bk7258_dma_m2p_cfg_s cfg;
  int ret;

  if (priv->closing || priv->stopping || priv->faulted ||
      priv->fault_pending || !priv->dac_running || priv->dma_active)
    {
      return OK;
    }

  apb = (FAR struct ap_buffer_s *)dq_peek(&priv->pendq);
  if (apb == NULL)
    {
      return OK;
    }

  cfg.dest         = bk7258_dac_fifo_addr();
  cfg.dest_req_mux = BK7258_DMA_REQ_AUDIO_DAC;

  /* The DAC FIFO port is a single 32-bit register (AUD_DAC_FPORT).  Each
   * word is one stereo frame {R:16, L:16}.  Both source and destination
   * widths must be 32-bit so the DMA engine drops exactly one word into the
   * FIFO per transfer beat.  Using 16-bit width would leave the FIFO
   * half-loaded and the "finish" interrupt would never fire.
   *
   * A mono (1-channel) stream therefore cannot be fed verbatim: packing two
   * consecutive mono samples into one word would make the single (left)
   * driven DAC replay every-other sample at 2x speed (octave-up).  Duplicate
   * each mono sample into both half-words in a ping-pong scratch area first,
   * then DMA the expanded stereo frame.
   */

  cfg.src_width    = BK7258_DMA_WIDTH_32;
  cfg.dst_width    = BK7258_DMA_WIDTH_32;

  if (priv->channels == 1 && priv->mono_expand_buf != NULL)
    {
      FAR uint8_t *scratch = (FAR uint8_t *)priv->mono_expand_buf
                             + (size_t)priv->expand_idx
                               * BK7258_AUDIO_BUFFER_SIZE * 2u;
      FAR uint32_t *stereo = (FAR uint32_t *)scratch;
      FAR const int16_t *mono = (FAR const int16_t *)apb->samp;
      uint32_t nsamples = apb->nbytes / 2u;
      uint32_t i;

      for (i = 0; i < nsamples; i++)
        {
          uint32_t s = (uint32_t)(uint16_t)mono[i];
          stereo[i] = (s << 16) | s;   /* {R:s, L:s} */
        }

      cfg.buf     = (uintptr_t)scratch;
      cfg.buf_len = (size_t)nsamples * 4u;
      priv->expand_idx ^= 1u;
    }
  else
    {
      cfg.buf     = (uintptr_t)apb->samp;
      cfg.buf_len = apb->nbytes;
    }

  /* AVDK's reference audio path uses REPEAT + src loop instead of a
   * one-shot transfer.  Empirical trace with one_shot=true showed the DMA
   * moves only 3 words then stalls (remain_len frozen).  In REPEAT mode
   * the engine keeps consuming DREQ pulses from the DAC FIFO until
   * transfer_len is reached, which matches the peripheral request timing
   * BK7258's audio block appears to expect.
   */

  cfg.one_shot     = false;
  cfg.callback     = bk7258_audio_dma_cb;
  cfg.arg          = priv;

  /* Clean the source buffer out of D-cache; GDMA reads from RAM. */

  up_clean_dcache(cfg.buf, cfg.buf + cfg.buf_len);

  ret = bk7258_gdma_start_m2p(priv->dma_ch, &cfg);
  if (ret < 0)
    {
      AUD_ERR("gdma_start_m2p failed: %d", ret);
      return ret;
    }

  priv->dma_active = true;
  return OK;
}

/* LPWORK is deliberately not a queue advancer.  It only makes an
 * asynchronous fault safe, returns completed/failed buffers, and emits
 * deferred upper-half notifications.
 */

static void bk7258_worker(FAR void *arg)
{
  FAR struct bk7258_audio_s *priv = arg;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;
  bool complete;
  bool fault_underrun;
  bool fault_work;
  bool underrun;
  int error;
  int pa_ret;

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  error = priv->dma_error < 0 ? priv->dma_error : -EIO;
  fault_work = priv->fault_pending;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (fault_work)
    {
      if (priv->configured)
        {
          (void)bk7258_dac_mute(true);
        }

      if (priv->dma_ch >= 0)
        {
          bk7258_gdma_stop(priv->dma_ch);
        }

      bk7258_dac_stop();
      pa_ret = bk7258_pa_disable();
      if (pa_ret < 0)
        {
          AUD_ERR("PA disable after DMA fault failed: %d", pa_ret);
        }

      flags = spin_lock_irqsave(&priv->lock);
      priv->fault_pending = false;
      spin_unlock_irqrestore(&priv->lock, flags);
    }

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      if (priv->closing)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return;
        }

      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->doneq);
      spin_unlock_irqrestore(&priv->lock, flags);

      if (apb == NULL)
        {
          break;
        }

      bk7258_notify_dequeue(priv, apb, OK);
    }

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      if (priv->closing)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return;
        }

      /* A fault raised after this worker started must be made hardware-safe
       * by the requeued invocation before any error DEQUEUE is emitted.
       */

      if (priv->fault_pending)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return;
        }

      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->failq);
      spin_unlock_irqrestore(&priv->lock, flags);

      if (apb == NULL)
        {
          break;
        }

      bk7258_notify_dequeue(priv, apb, error);
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  fault_underrun = priv->faulted || fault_work;
  underrun = priv->underrun_pending;
  priv->underrun_pending = false;

  /* A synchronous initial-start failure is recoverable once hardware is
   * safe and every accepted buffer has completed its error callback.  Keep
   * faulted asserted during those callbacks so upper-half refill attempts
   * are rejected.  Runtime faults never set recover_after_fault.
   */

  if (priv->recover_after_fault && !priv->fault_pending &&
      dq_peek(&priv->failq) == NULL)
    {
      priv->recover_after_fault = false;
      priv->faulted = false;
      priv->dma_error = OK;
    }

  if (priv->final_seen && !priv->faulted && !priv->dma_active &&
      dq_peek(&priv->pendq) == NULL)
    {
      priv->final_seen = false;
      priv->complete_pending = true;
    }

  complete = priv->complete_pending && !priv->dma_active &&
             dq_peek(&priv->pendq) == NULL &&
             dq_peek(&priv->doneq) == NULL &&
             dq_peek(&priv->failq) == NULL;
  if (complete)
    {
      priv->complete_pending = false;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (underrun)
    {
      bk7258_notify_underrun(priv, fault_underrun ? error : -EPIPE);
    }

  if (complete)
    {
      bk7258_notify_complete(priv);
    }
}

/* DMA IRQ callback.  IRQ completion and enqueue are the only queue
 * advancers.  Faults only close the run gate and defer all sleeping, PA,
 * hardware-stop and upper-half work to LPWORK.
 */

void bk7258_audio_dma_cb(int ch, uint32_t event, FAR void *arg)
{
  FAR struct bk7258_audio_s *priv = arg;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;
  bool need_worker = false;
  int ret;

  (void)ch;

  flags = spin_lock_irqsave(&priv->lock);

  if ((event & BK7258_DMA_EVENT_BUS_ERROR) != 0u)
    {
      if (priv->dac_running || priv->dma_active)
        {
          AUD_ERR("DMA bus error");
          bk7258_fault_locked(priv, -EIO, false);
          need_worker = true;
        }
    }
  else if ((event & BK7258_DMA_EVENT_FULL) != 0u && priv->dma_active)
    {
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
      priv->dma_active = false;
      if (apb != NULL)
        {
          dq_addlast(&apb->dq_entry, &priv->doneq);
          need_worker = true;
        }

      if (priv->dac_running && !priv->closing && !priv->stopping &&
          !priv->faulted)
        {
          ret = bk7258_start_next_locked(priv);
          if (ret < 0)
            {
              bk7258_fault_locked(priv, ret, false);
              need_worker = true;
            }
          else if (!priv->dma_active)
            {
              if (priv->final_seen)
                {
                  priv->complete_pending = true;
                }
              else
                {
                  priv->underrun_pending = true;
                }

              need_worker = true;
            }
        }
    }

  if (need_worker)
    {
      bk7258_schedule_worker_locked(priv);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}

/****************************************************************************
 * audio_ops_s callbacks
 ****************************************************************************/

static int bk7258_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                          FAR struct audio_caps_s *caps)
{
  (void)dev;
  (void)type;

  DEBUGASSERT(caps->ac_len >= sizeof(struct audio_caps_s));

  caps->ac_format.hw   = 0;
  caps->ac_controls.w  = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        caps->ac_channels = AUDIO_CHANNELS_RANGE(1, 2);
        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_FEATURE;
              caps->ac_format.hw     = 1 << (AUDIO_FMT_PCM - 1);
              break;

            case AUDIO_FMT_PCM:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_PCM_S16_LE;
              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }
        break;

      case AUDIO_TYPE_OUTPUT:
        caps->ac_channels = AUDIO_CHANNELS_RANGE(1, 2);
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K
                                    | AUDIO_SAMP_RATE_16K
                                    | AUDIO_SAMP_RATE_44K
                                    | AUDIO_SAMP_RATE_48K;
          }
        break;

      case AUDIO_TYPE_FEATURE:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_FU_VOLUME | AUDIO_FU_MUTE;
          }
        break;

      default:
        caps->ac_subtype  = 0;
        caps->ac_channels = 0;
        break;
    }

  return caps->ac_len;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_configure(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session,
                            FAR const struct audio_caps_s *caps)
#else
static int bk7258_configure(FAR struct audio_lowerhalf_s *dev,
                            FAR const struct audio_caps_s *caps)
#endif
{
  FAR struct bk7258_audio_s *priv = (FAR struct bk7258_audio_s *)dev;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  (void)session;
#endif

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
        {
          irqstate_t flags;
          uint32_t sample_rate;
          uint8_t bpsamp;
          uint8_t channels;

          sample_rate = caps->ac_controls.hw[0]
                      | ((uint32_t)caps->ac_controls.b[3] << 16);
          channels = caps->ac_channels;
          bpsamp = caps->ac_controls.b[2];

          if (bpsamp == 0)
            {
              bpsamp = BK7258_AUDIO_DEFAULT_BPSAMP;
            }

          if (sample_rate == 0)
            {
              sample_rate = BK7258_AUDIO_DEFAULT_SAMPLE_RATE;
            }

          if (channels == 0)
            {
              channels = BK7258_AUDIO_DEFAULT_CHANNELS;
            }

          if (bpsamp != 16)
            {
              AUD_ERR("only S16_LE supported (asked for %u)", bpsamp);
              return -EINVAL;
            }

          flags = spin_lock_irqsave(&priv->lock);
          if (priv->closing)
            {
              spin_unlock_irqrestore(&priv->lock, flags);
              return -ESHUTDOWN;
            }

          /* Keep a fault latched through hardware teardown and every error
           * DEQUEUE.  A persistent runtime fault additionally requires stop
           * to have closed the refill gate before OUTPUT configure recovers
           * it.
           */

          if (priv->faulted &&
              (priv->fault_pending || dq_peek(&priv->failq) != NULL))
            {
              spin_unlock_irqrestore(&priv->lock, flags);
              return -EBUSY;
            }

          if (priv->faulted && !priv->recover_after_fault &&
              !priv->stopping)
            {
              int error = priv->dma_error < 0 ? priv->dma_error : -EIO;
              spin_unlock_irqrestore(&priv->lock, flags);
              return error;
            }

          priv->sample_rate = sample_rate;
          priv->channels = channels;
          priv->bpsamp = bpsamp;

          /* Successful OUTPUT configure reopens a normal stop/shutdown or a
           * fully drained persistent-fault teardown.
           */

          priv->stopping = false;
          priv->faulted = false;
          priv->recover_after_fault = false;
          priv->complete_pending = false;
          priv->final_seen = false;
          priv->underrun_pending = false;
          priv->dma_error = OK;

          spin_unlock_irqrestore(&priv->lock, flags);

          AUD_INFO("configure OUTPUT rate=%lu ch=%u bps=%u",
                   (unsigned long)sample_rate, channels, bpsamp);
        }
        break;

      case AUDIO_TYPE_FEATURE:
        {
          irqstate_t flags;
          int ret = OK;

          flags = spin_lock_irqsave(&priv->lock);
          switch (caps->ac_format.hw)
            {
              case AUDIO_FU_VOLUME:
                {
                  uint16_t v = caps->ac_controls.hw[0];
                  if (v > 100)
                    {
                      v = 100;
                    }

                  priv->volume = (uint8_t)v;
                }
                break;

              case AUDIO_FU_MUTE:
                priv->mute = caps->ac_controls.b[0] != 0;
                break;

              default:
                spin_unlock_irqrestore(&priv->lock, flags);
                return -EINVAL;
            }

          /* Always cache feature settings, but touch hardware only for a
           * healthy running stream.  The check and bounded register update
           * share the fault-state lock so a worker mute cannot be undone by
           * a concurrent feature configure.
           */

          if (priv->configured && priv->dac_running && !priv->closing &&
              !priv->stopping && !priv->faulted && !priv->fault_pending)
            {
              ret = bk7258_apply_volume(priv);
              if (ret < 0)
                {
                  /* An analog SPI failure leaves the DAC mute state
                   * unknown.  Fail closed: stop accepting/advancing buffers
                   * immediately and let LPWORK turn off DMA, DAC and PA.
                   * Keep the original register error visible to the ioctl.
                   */

                  bk7258_fault_locked(priv, ret, false);
                  bk7258_schedule_worker_locked(priv);
                }
            }

          spin_unlock_irqrestore(&priv->lock, flags);
          return ret;
        }

      default:
        return -ENOSYS;
    }

  return OK;
}

static int bk7258_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_audio_s *priv = (FAR struct bk7258_audio_s *)dev;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;
  int error;
  int pa_ret;

  /* Temporarily close every producer/run gate before touching hardware.
   * Queueing LPWORK is serialized by this same lock.  The barrier is lifted
   * only after IRQ/worker teardown and all owned buffers are returned.
   */

  flags = spin_lock_irqsave(&priv->lock);
  priv->closing = true;
  priv->dac_running = false;
  priv->dma_active = false;
  priv->complete_pending = false;
  priv->underrun_pending = false;
  priv->final_seen = false;
  error = priv->dma_error < 0 ? priv->dma_error : -ESHUTDOWN;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Stop the IRQ producer first, then synchronously cover any worker queued
   * before closing became visible.  Neither operation runs under a spinlock.
   */

  if (priv->dma_ch >= 0)
    {
      bk7258_gdma_stop(priv->dma_ch);
    }

  (void)work_cancel_sync(LPWORK, &priv->work);

  if (priv->configured)
    {
      (void)bk7258_dac_mute(true);
    }

  bk7258_dac_stop();
  pa_ret = bk7258_pa_disable();
  if (pa_ret < 0)
    {
      AUD_ERR("PA disable during shutdown failed: %d", pa_ret);
    }

  if (priv->dma_ch >= 0)
    {
      bk7258_gdma_free(priv->dma_ch);
      priv->dma_ch = -1;
    }

  if (priv->mono_expand_buf != NULL)
    {
      kmm_free(priv->mono_expand_buf);
      priv->mono_expand_buf = NULL;
      priv->expand_idx = 0;
    }

  if (priv->configured)
    {
      bk7258_dac_deinit();
    }

  /* audio_close keeps upper->status alive until shutdown returns.  Return
   * all lower-owned buffers synchronously; callback refill attempts observe
   * closing and are rejected without transferring ownership back to us.
   */

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->doneq);
      spin_unlock_irqrestore(&priv->lock, flags);
      if (apb == NULL)
        {
          break;
        }

      bk7258_notify_dequeue(priv, apb, OK);
    }

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
      spin_unlock_irqrestore(&priv->lock, flags);
      if (apb == NULL)
        {
          break;
        }

      bk7258_notify_dequeue(priv, apb, -ESHUTDOWN);
    }

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->failq);
      spin_unlock_irqrestore(&priv->lock, flags);
      if (apb == NULL)
        {
          break;
        }

      bk7258_notify_dequeue(priv, apb, error);
    }

  /* GDMA is stopped/freed and work_cancel_sync has covered every queued
   * worker, so no IRQ or worker can observe the fault reset below.  Keep the
   * normal stop gate closed until the next successful OUTPUT configure, but
   * make the registered lower half reusable by a future open.
   */

  flags = spin_lock_irqsave(&priv->lock);
  priv->configured = false;
  priv->closing = false;
  priv->stopping = true;
  priv->faulted = false;
  priv->fault_pending = false;
  priv->recover_after_fault = false;
  priv->complete_pending = false;
  priv->underrun_pending = false;
  priv->final_seen = false;
  priv->dma_error = OK;
  spin_unlock_irqrestore(&priv->lock, flags);

  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_start(FAR struct audio_lowerhalf_s *dev,
                        FAR void *session)
#else
static int bk7258_start(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct bk7258_audio_s *priv = (FAR struct bk7258_audio_s *)dev;
  struct bk7258_dac_cfg_s dac_cfg;
  irqstate_t flags;
  bool notify_fault = false;
  int pa_ret;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  (void)session;
#endif

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ESHUTDOWN;
    }

  if (priv->stopping || priv->fault_pending)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }

  if (priv->faulted)
    {
      ret = priv->dma_error < 0 ? priv->dma_error : -EIO;
      spin_unlock_irqrestore(&priv->lock, flags);
      return ret;
    }

  if (priv->dac_running)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  /* Program the DAC block.  Start muted; unmute happens only after PA
   * enable and both of its board-owned settling delays succeed.
   */

  dac_cfg.samp_rate = priv->sample_rate;
  dac_cfg.channel   = BK7258_DAC_CHL_LR;
  dac_cfg.work_mode = BK7258_DAC_WORK_DIFFEN;
  dac_cfg.gain      = 0;
  dac_cfg.use_apll  = true;

  ret = bk7258_dac_configure(&dac_cfg);
  if (ret < 0)
    {
      AUD_ERR("dac_configure failed: %d", ret);

      /* configure powers the analog path incrementally.  Roll back even
       * though configured was never published, so an intermediate failure
       * cannot leave DAC rails enabled.
       */

      (void)bk7258_dac_mute(true);
      bk7258_dac_stop();
      pa_ret = bk7258_pa_disable();
      if (pa_ret < 0)
        {
          AUD_ERR("PA disable after DAC configure failure: %d", pa_ret);
        }

      bk7258_dac_deinit();
      flags = spin_lock_irqsave(&priv->lock);
      priv->configured = false;
      priv->dma_active = false;
      spin_unlock_irqrestore(&priv->lock, flags);
      return ret;
    }

  /* The chip configure path may leave the digital DAC unmuted.  Assert mute
   * immediately, before publishing configured or enabling the PA.
   */

  ret = bk7258_dac_mute(true);
  if (ret < 0)
    {
      AUD_ERR("dac_mute failed after configure: %d", ret);
      goto errout;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->configured = true;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (priv->dma_ch < 0)
    {
      priv->dma_ch = bk7258_gdma_alloc();
      if (priv->dma_ch < 0)
        {
          ret = priv->dma_ch;
          AUD_ERR("gdma_alloc failed: %d", ret);
          goto errout;
        }
    }

  /* Mono playback needs a stereo-expansion scratch (see start_next_locked).
   * Allocate two ping-pong areas once per stream; they are freed on shutdown. */

  if (priv->channels == 1 && priv->mono_expand_buf == NULL)
    {
      priv->mono_expand_buf = kmm_memalign(32,
                                           2u * BK7258_AUDIO_BUFFER_SIZE * 2u);
      if (priv->mono_expand_buf == NULL)
        {
          ret = -ENOMEM;
          AUD_ERR("mono expand buffer alloc failed");
          goto errout;
        }

      priv->expand_idx = 0;
    }

  /* Settle the PA while the configured DAC is still muted and before the
   * run gate allows any DMA transfer.  This avoids consuming queued audio
   * during the board-owned delays and leaves no asynchronous DMA fault for
   * start() to race with.
   */

  ret = bk7258_pa_enable();
  if (ret < 0)
    {
      AUD_ERR("PA enable failed: %d", ret);
      goto errout;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing || priv->stopping || priv->faulted ||
      priv->fault_pending)
    {
      ret = priv->closing ? -ESHUTDOWN :
            (priv->faulted && priv->dma_error < 0 ? priv->dma_error :
             -EBUSY);
      spin_unlock_irqrestore(&priv->lock, flags);
      goto errout;
    }

  priv->dac_running = true;
  priv->dma_active = false;
  priv->underrun_pending = false;

  ret = bk7258_start_next_locked(priv);
  if (ret < 0)
    {
      bk7258_fault_locked(priv, ret, true);
      notify_fault = true;
      spin_unlock_irqrestore(&priv->lock, flags);
      goto errout;
    }

  /* These are non-sleeping register operations.  Keep IRQs disabled until
   * the DAC is started and volume applied so no DMA fault can close the run
   * gate between the final state check and unmute.
   */

  bk7258_dac_start();
  ret = bk7258_apply_volume(priv);
  if (ret < 0)
    {
      AUD_ERR("apply volume during start failed: %d", ret);
      bk7258_fault_locked(priv, ret, true);
      notify_fault = true;
      spin_unlock_irqrestore(&priv->lock, flags);
      goto errout;
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;

errout:
  /* Complete rollback.  PA disable errors are diagnostic only here: the
   * initiating start error remains the caller-visible result.
   */

  (void)bk7258_dac_mute(true);
  pa_ret = bk7258_pa_disable();
  if (pa_ret < 0)
    {
      AUD_ERR("PA disable during start rollback failed: %d", pa_ret);
    }

  bk7258_dac_stop();
  if (priv->dma_ch >= 0)
    {
      bk7258_gdma_stop(priv->dma_ch);
      bk7258_gdma_free(priv->dma_ch);
      priv->dma_ch = -1;
    }

  bk7258_dac_deinit();

  flags = spin_lock_irqsave(&priv->lock);
  priv->configured = false;
  priv->dma_active = false;
  if (notify_fault)
    {
      bk7258_schedule_worker_locked(priv);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return ret;
}

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_stop(FAR struct audio_lowerhalf_s *dev,
                       FAR void *session)
#else
static int bk7258_stop(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct bk7258_audio_s *priv = (FAR struct bk7258_audio_s *)dev;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;
  int pa_ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  (void)session;
#endif

  /* stopping remains asserted through asynchronous DEQUEUE/COMPLETE so an
   * upper-half refill caused by those callbacks cannot be reacquired.  A
   * later successful OUTPUT configure opens this gate again.
   */

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ESHUTDOWN;
    }

  priv->stopping = true;
  priv->dac_running = false;
  priv->dma_active = false;
  priv->complete_pending = false;
  priv->underrun_pending = false;
  priv->final_seen = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (priv->configured)
    {
      (void)bk7258_dac_mute(true);
    }

  if (priv->dma_ch >= 0)
    {
      bk7258_gdma_stop(priv->dma_ch);
    }

  bk7258_dac_stop();
  pa_ret = bk7258_pa_disable();
  if (pa_ret < 0)
    {
      AUD_ERR("PA disable during stop failed: %d", pa_ret);
    }

  flags = spin_lock_irqsave(&priv->lock);

  /* Return every normally stopped buffer successfully.  Buffers already
   * detached into failq retain their asynchronous error result.
   */

  while ((apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq)) != NULL)
    {
      dq_addlast(&apb->dq_entry, &priv->doneq);
    }

  priv->complete_pending = true;
  bk7258_schedule_worker_locked(priv);
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}
#endif

static int bk7258_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                FAR struct ap_buffer_s *apb)
{
  FAR struct bk7258_audio_s *priv = (FAR struct bk7258_audio_s *)dev;
  irqstate_t flags;
  int ret;

  DEBUGASSERT(priv && apb);

  flags = spin_lock_irqsave(&priv->lock);

  /* Rejection happens before ownership transfer.  In particular, faulted
   * remains latched while error callbacks make upper-half refill attempts.
   */

  if (priv->closing)
    {
      ret = -ESHUTDOWN;
      goto reject;
    }

  if (priv->stopping || priv->fault_pending)
    {
      ret = -EBUSY;
      goto reject;
    }

  if (priv->faulted)
    {
      ret = priv->dma_error < 0 ? priv->dma_error : -EIO;
      goto reject;
    }

  if ((apb->flags & AUDIO_APB_FINAL) != 0u)
    {
      priv->final_seen = true;
    }

  dq_addlast(&apb->dq_entry, &priv->pendq);

  /* Once queued, ownership has transferred.  A synchronous start failure is
   * reported asynchronously by error DEQUEUE; returning an error here as
   * well would make ownership ambiguous to the upper half.
   */

  if (priv->dac_running && !priv->dma_active)
    {
      ret = bk7258_start_next_locked(priv);
      if (ret < 0)
        {
          bk7258_fault_locked(priv, ret, false);
          bk7258_schedule_worker_locked(priv);
        }
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;

reject:
  spin_unlock_irqrestore(&priv->lock, flags);
  return ret;
}

static int bk7258_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                               FAR struct ap_buffer_s *apb)
{
  (void)dev;
  (void)apb;

  /* Buffers currently owned by DMA cannot be cancelled without a full
   * stop.  Best-effort: report success; the buffer will be dequeued in
   * order.  The upper half is free to call stop() for a hard abort.
   */

  return OK;
}

static int bk7258_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                        unsigned long arg)
{
  (void)dev;

  switch (cmd)
    {
      case AUDIOIOC_GETBUFFERINFO:
        {
          FAR struct ap_buffer_info_s *info =
              (FAR struct ap_buffer_info_s *)arg;

          info->nbuffers    = BK7258_AUDIO_NUM_BUFFERS;
          info->buffer_size = BK7258_AUDIO_BUFFER_SIZE;
        }
        return OK;

      default:
        return -ENOTTY;
    }
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_reserve(FAR struct audio_lowerhalf_s *dev,
                          FAR void **session)
{
  (void)dev;
  if (session != NULL)
    {
      *session = NULL;
    }
  return OK;
}

static int bk7258_release(FAR struct audio_lowerhalf_s *dev,
                          FAR void *session)
{
  (void)dev;
  (void)session;
  return OK;
}
#else
static int bk7258_reserve(FAR struct audio_lowerhalf_s *dev)
{
  (void)dev;
  return OK;
}

static int bk7258_release(FAR struct audio_lowerhalf_s *dev)
{
  (void)dev;
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_audio_playback_initialize(void)
{
  FAR struct bk7258_audio_s *priv;
  int ret;

  priv = kmm_zalloc(sizeof(struct bk7258_audio_s));
  if (priv == NULL)
    {
      AUD_ERR("alloc failed");
      return -ENOMEM;
    }

  priv->dev.ops     = &g_bk7258_playback_ops;
  priv->type        = BK7258_AUDIO_TYPE_PLAYBACK;
  priv->sample_rate = BK7258_AUDIO_DEFAULT_SAMPLE_RATE;
  priv->channels    = BK7258_AUDIO_DEFAULT_CHANNELS;
  priv->bpsamp      = BK7258_AUDIO_DEFAULT_BPSAMP;
  priv->volume      = BK7258_AUDIO_DEFAULT_VOLUME;
  priv->mute        = false;
  priv->dma_ch      = -1;
  spin_lock_init(&priv->lock);
  dq_init(&priv->pendq);
  dq_init(&priv->doneq);
  dq_init(&priv->failq);

  /* Bring the GDMA subsystem up eagerly so the first start() does not have
   * to serialise on it inside the audio-open critical path.
   */

  ret = bk7258_gdma_initialize();
  if (ret < 0)
    {
      AUD_ERR("gdma_initialize failed: %d", ret);
      kmm_free(priv);
      return ret;
    }

  ret = audio_register("pcm0p", &priv->dev);
  if (ret < 0)
    {
      AUD_ERR("audio_register failed: %d", ret);
      kmm_free(priv);
      return ret;
    }

  AUD_INFO("registered /dev/audio/pcm0p");
  return OK;
}

/****************************************************************************
 * Capture lower-half (/dev/audio/pcm0c)
 *
 * Armino records through GDMA, not FIFO polling: ap/components/audio_record/
 * src/onboard_mic_record.c:440-506 allocates a channel with
 * bk_dma_alloc(DMA_DEV_AUDIO), takes the FIFO port from
 * bk_aud_adc_get_fifo_addr(), and lets DMA_WORK_MODE_REPEAT move samples
 * into a RAM ring with a finish ISR.  The shape here is the same engine
 * with the per-buffer envelope the playback half above already uses.
 ****************************************************************************/

/* read() mode.  ai_agent's NuttX backend pulls PCM with read() instead of
 * the AUDIOIOC_ENQUEUEBUFFER/DEQUEUE message loop nxrecorder uses.  The
 * first read() switches the device into a driver-owned ring: the same GDMA
 * engine fills rdbuf[] in turn, a filled buffer moves to doneq, and read()
 * copies out of it and hands it back to pendq.  The two modes are
 * exclusive per session; stop() ends either.
 */

#define CAPTURE_READ_NBUF        3
#define CAPTURE_READ_TIMEOUT_MS  500

struct bk7258_capture_s
{
  struct audio_lowerhalf_s dev;
  spinlock_t               lock;
  dq_queue_t               pendq;
  dq_queue_t               doneq;    /* read() mode: filled, not yet read */
  sem_t                    readsem;  /* posted per filled buffer and on stop */
  bool                     readmode;
  struct ap_buffer_s       rdbuf[CAPTURE_READ_NBUF];
  volatile bool            running;
  bool                     paused;
  bool                     stopping;
  bool                     dma_active;
  bool                     complete_sent;
  int                      dma_ch;
  FAR struct ap_buffer_s  *dma_apb;
  uint32_t                 samprate;
  uint8_t                  channels;
  uint8_t                  bps;
  uint8_t                  volume;
};

static struct bk7258_capture_s g_capture;

static int  capture_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                            FAR struct audio_caps_s *caps);
static int  capture_configure(FAR struct audio_lowerhalf_s *dev,
                              FAR const struct audio_caps_s *caps);
static int  capture_start(FAR struct audio_lowerhalf_s *dev);
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int  capture_pause(FAR struct audio_lowerhalf_s *dev);
static int  capture_resume(FAR struct audio_lowerhalf_s *dev);
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int  capture_stop(FAR struct audio_lowerhalf_s *dev);
#endif
static int  capture_shutdown(FAR struct audio_lowerhalf_s *dev);
static int  capture_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                  FAR struct ap_buffer_s *apb);
static int  capture_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct ap_buffer_s *apb);
static int  capture_reserve(FAR struct audio_lowerhalf_s *dev);
static int  capture_release(FAR struct audio_lowerhalf_s *dev);
static int  capture_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                          unsigned long arg);
static int  capture_read(FAR struct audio_lowerhalf_s *dev,
                         FAR char *buffer, size_t buflen);
static void capture_dma_cb(int ch, uint32_t event, FAR void *arg);
static void capture_start_dma(FAR struct bk7258_capture_s *priv);

static const struct audio_ops_s g_bk7258_capture_ops =
{
  capture_getcaps,
  capture_configure,
  capture_shutdown,
  capture_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  capture_stop,
#else
  NULL,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  capture_pause,
  capture_resume,
#else
  NULL,
  NULL,
#endif
  NULL,                  /* allocbuffer    */
  NULL,                  /* freebuffer     */
  capture_enqueuebuffer,
  capture_cancelbuffer,
  capture_ioctl,
  capture_read,
  NULL,                  /* write          */
  capture_reserve,
  capture_release
};

static void capture_notify_dequeue(FAR struct bk7258_capture_s *priv,
                                   FAR struct ap_buffer_s *apb, int result)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, result, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, result);
#endif
}

/* Same shape as the playback half's bk7258_volume_to_gain(): the upper
 * half speaks 0..100 and the hardware wants the 6-bit ADC gain 0..0x3F.
 * Volume 72 rounds to 45 = 0x2d, Armino's DEFAULT_AUD_ADC_CONFIG().adc_gain.
 */

#define BK7258_ADC_VOLUME_DEFAULT  72u
#define BK7258_ADC_GAIN_MAX        0x3Fu

static uint8_t capture_volume_to_gain(uint8_t volume)
{
  return (uint8_t)(((uint32_t)volume * BK7258_ADC_GAIN_MAX) / 100u);
}

static void capture_notify_complete(FAR struct bk7258_capture_s *priv)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif
}

static void capture_start_dma(FAR struct bk7258_capture_s *priv)
{
  FAR struct ap_buffer_s *apb;
  struct bk7258_dma_p2m_cfg_s cfg;
  int ret;

  /* Buffers queued before AUDIOIOC_START wait here; capture_start() kicks
   * the DMA once the ADC is running.
   */

  if (!priv->running || priv->paused || priv->stopping ||
      priv->dma_active || priv->dma_ch < 0)
    {
      return;
    }

  apb = (FAR struct ap_buffer_s *)dq_peek(&priv->pendq);
  if (apb == NULL)
    {
      return;
    }

  cfg.buf         = (uintptr_t)apb->samp;
  cfg.buf_len     = apb->nmaxbytes;
  cfg.src         = bk7258_adc_fifo_addr();
  cfg.src_req_mux = BK7258_DMA_REQ_AUDIO_ADC;
  /* AUD_ADC_FPORT is a single 32-bit register; one beat is a stereo frame
   * or two mono samples.  Both widths 32-bit, same rule as the playback
   * path documents for AUD_DAC_FPORT.
   */

  cfg.src_width   = BK7258_DMA_WIDTH_32;
  cfg.dst_width   = BK7258_DMA_WIDTH_32;
  cfg.one_shot    = true;
  cfg.callback    = capture_dma_cb;
  cfg.arg         = priv;

  ret = bk7258_gdma_start_p2m(priv->dma_ch, &cfg);
  if (ret < 0)
    {
      AUD_ERR("gdma_start_p2m failed: %d", ret);
      return;
    }

  priv->dma_active = true;
  priv->dma_apb    = apb;
}

static void capture_compact_mono(FAR struct ap_buffer_s *apb)
{
  FAR int16_t *samples = (FAR int16_t *)apb->samp;
  uint32_t frames = (uint32_t)apb->nmaxbytes / 4u;
  uint32_t i;

  for (i = 0; i < frames; i++)
    {
      samples[i] = samples[2 * i];
    }

  apb->nbytes = (uint32_t)apb->nmaxbytes / 2u;
}

static void capture_dma_cb(int ch, uint32_t event, FAR void *arg)
{
  FAR struct bk7258_capture_s *priv = arg;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;

  (void)ch;

  flags = spin_lock_irqsave(&priv->lock);
  apb = priv->dma_apb;
  priv->dma_apb    = NULL;
  priv->dma_active = false;

  if (apb != NULL && (event & BK7258_DMA_EVENT_BUS_ERROR) == 0)
    {
#ifdef CONFIG_ARMV8M_DCACHE
      up_invalidate_dcache((uintptr_t)apb->samp,
                           (uintptr_t)apb->samp + apb->nmaxbytes);
#endif
      capture_compact_mono(apb);
#ifdef CONFIG_ARMV8M_DCACHE
      up_clean_dcache((uintptr_t)apb->samp,
                      (uintptr_t)apb->samp + apb->nbytes);
#endif

      dq_rem(&apb->dq_entry, &priv->pendq);
      if (priv->readmode)
        {
          apb->curbyte = 0;
          dq_addlast(&apb->dq_entry, &priv->doneq);
          spin_unlock_irqrestore(&priv->lock, flags);
          nxsem_post(&priv->readsem);
        }
      else
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          capture_notify_dequeue(priv, apb, OK);
        }
    }
  else
    {
      spin_unlock_irqrestore(&priv->lock, flags);
    }

  capture_start_dma(priv);
}

static int capture_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                           FAR struct audio_caps_s *caps)
{
  (void)dev;
  (void)type;

  /* nxrecorder picks a device by asking AUDIO_TYPE_QUERY and then testing
   * ac_controls.b[0] & AUDIO_TYPE_INPUT (nxrecorder.c:206).  Answering
   * with ac_type = AUDIO_TYPE_INPUT instead leaves b[0] zero and the
   * search reports "No suitable Audio Device".
   */

  caps->ac_format.hw   = 0;
  caps->ac_controls.w  = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        caps->ac_channels = 1;
        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] = AUDIO_TYPE_INPUT | AUDIO_TYPE_FEATURE;
              caps->ac_format.hw     = 1 << (AUDIO_FMT_PCM - 1);
              break;

            case AUDIO_FMT_PCM:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_PCM_S16_LE;
              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }
        break;

      case AUDIO_TYPE_INPUT:
        caps->ac_channels = 1;
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K
                                    | AUDIO_SAMP_RATE_16K
                                    | AUDIO_SAMP_RATE_44K
                                    | AUDIO_SAMP_RATE_48K;
          }
        break;

      case AUDIO_TYPE_FEATURE:
        /* nxplayer's volume command is an AUDIOIOC_CONFIGURE with
         * ac_type=AUDIO_TYPE_FEATURE and ac_format.hw=AUDIO_FU_VOLUME
         * (nxplayer.c:1316-1322), not a dedicated ioctl.  Advertising the
         * unit here is what makes the setting reach the driver at all.
         */

        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_FU_VOLUME;
          }
        break;
    }

  return sizeof(struct audio_caps_s);
}

static int capture_configure(FAR struct audio_lowerhalf_s *dev,
                             FAR const struct audio_caps_s *caps)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  struct bk7258_adc_cfg_s cfg;

  if (caps->ac_type == AUDIO_TYPE_FEATURE)
    {
      if (caps->ac_format.hw != AUDIO_FU_VOLUME)
        {
          return -EINVAL;
        }

      {
        uint16_t v = caps->ac_controls.hw[0];
        if (v > 100)
          {
            v = 100;
          }

        priv->volume = (uint8_t)v;
      }

      return bk7258_adc_set_gain(capture_volume_to_gain(priv->volume));
    }

  if (caps->ac_type != AUDIO_TYPE_INPUT)
    {
      return -EINVAL;
    }

  priv->samprate = caps->ac_controls.hw[0]
                 | ((uint32_t)caps->ac_controls.b[3] << 16);
  priv->channels = caps->ac_channels;
  priv->bps = caps->ac_controls.b[2];

  memset(&cfg, 0, sizeof(cfg));
  cfg.samp_rate = priv->samprate;
  cfg.channel   = BK7258_ADC_CHL_L;
  cfg.gain      = capture_volume_to_gain(priv->volume);
  cfg.samp_edge = false;
  cfg.use_apll  = true;

  return bk7258_adc_configure(&cfg);
}

static int capture_start(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;

  priv->running       = true;
  priv->stopping      = false;
  priv->paused        = false;
  priv->complete_sent = false;
  bk7258_adc_start();
  capture_start_dma(priv);
  return OK;
}

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int capture_pause(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;

  priv->paused = true;
  if (priv->dma_active)
    {
      bk7258_gdma_stop(priv->dma_ch);
      priv->dma_active = false;
      priv->dma_apb    = NULL;
    }

  bk7258_adc_stop();
  return OK;
}

static int capture_resume(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;

  priv->paused = false;
  if (priv->running)
    {
      bk7258_adc_start();
      capture_start_dma(priv);
    }

  return OK;
}
#endif

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int capture_stop(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;

  priv->running  = false;
  priv->stopping = true;

  if (priv->dma_active)
    {
      bk7258_gdma_stop(priv->dma_ch);
      priv->dma_active = false;
      priv->dma_apb    = NULL;
    }

  bk7258_adc_stop();

  /* read() mode: the ring belongs to the driver, so nothing goes back to
   * the upper half.  Drop it and wake a blocked reader, which then sees
   * readmode cleared and returns -EAGAIN.
   */

  if (priv->readmode)
    {
      flags = spin_lock_irqsave(&priv->lock);
      dq_init(&priv->pendq);
      dq_init(&priv->doneq);
      priv->readmode = false;
      spin_unlock_irqrestore(&priv->lock, flags);
      nxsem_post(&priv->readsem);
    }

  /* nxrecorder's AUDIO_MSG_STOP handler stops streaming but stays in its
   * loop until AUDIO_MSG_COMPLETE arrives (nxrecorder.c:775).  Returning
   * the parked buffers alone is not enough: without COMPLETE the
   * recordthread never exits and nxrecorder_stop()'s pthread_join blocks
   * forever.  Same contract the playback half honours via
   * bk7258_notify_complete().
   */

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
      spin_unlock_irqrestore(&priv->lock, flags);
      if (apb == NULL)
        {
          break;
        }

      capture_notify_dequeue(priv, apb, -ECANCELED);
    }

  if (!priv->complete_sent)
    {
      priv->complete_sent = true;
      capture_notify_complete(priv);
    }

  return OK;
}
#endif

static int capture_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  return capture_stop(dev);
}

static int capture_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct ap_buffer_s *apb)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  dq_addlast(&apb->dq_entry, &priv->pendq);
  spin_unlock_irqrestore(&priv->lock, flags);

  capture_start_dma(priv);
  return OK;
}

static int capture_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                FAR struct ap_buffer_s *apb)
{
  capture_notify_dequeue((FAR struct bk7258_capture_s *)dev, apb, -ECANCELED);
  return OK;
}

static int capture_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                         unsigned long arg)
{
  (void)dev;

  switch (cmd)
    {
      case AUDIOIOC_GETBUFFERINFO:
        {
          FAR struct ap_buffer_info_s *info =
              (FAR struct ap_buffer_info_s *)arg;

          info->nbuffers    = BK7258_AUDIO_NUM_BUFFERS;
          info->buffer_size = BK7258_AUDIO_BUFFER_SIZE;
        }
        return OK;

      default:
        return -ENOTTY;
    }
}

/* Allocate the read() ring once and keep it: 3 x 4 KB, reused by every
 * session.  The lines are invalidated up front so a dirty line left by the
 * heap's previous owner cannot be evicted over DMA data.
 */

static int capture_read_alloc(FAR struct bk7258_capture_s *priv)
{
  int i;

  for (i = 0; i < CAPTURE_READ_NBUF; i++)
    {
      FAR struct ap_buffer_s *apb = &priv->rdbuf[i];

      if (apb->samp != NULL)
        {
          continue;
        }

      apb->samp = kmm_memalign(32, BK7258_AUDIO_BUFFER_SIZE);
      if (apb->samp == NULL)
        {
          return -ENOMEM;
        }

      apb->nmaxbytes = BK7258_AUDIO_BUFFER_SIZE;
      up_invalidate_dcache((uintptr_t)apb->samp,
                           (uintptr_t)apb->samp + apb->nmaxbytes);
    }

  return OK;
}

/* Blocks for at most CAPTURE_READ_TIMEOUT_MS.  Returns -EAGAIN on timeout
 * or once stop() has ended the session, never 0: ai_agent's recording loop
 * treats 0 as end of stream and -EAGAIN as "retry".
 */

static int capture_read(FAR struct audio_lowerhalf_s *dev,
                        FAR char *buffer, size_t buflen)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;
  bool requeue = false;
  bool readmode;
  size_t n;
  int ret;
  int i;

  flags = spin_lock_irqsave(&priv->lock);
  readmode = priv->readmode;
  if (!readmode && !dq_empty(&priv->pendq))
    {
      /* nxrecorder-style buffers are queued: the session is taken. */

      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (!readmode)
    {
      ret = capture_read_alloc(priv);
      if (ret < 0)
        {
          return ret;
        }

      /* Posts the last session left unconsumed must not carry over. */

      nxsem_reset(&priv->readsem, 0);

      flags = spin_lock_irqsave(&priv->lock);
      dq_init(&priv->doneq);
      for (i = 0; i < CAPTURE_READ_NBUF; i++)
        {
          priv->rdbuf[i].curbyte = 0;
          dq_addlast(&priv->rdbuf[i].dq_entry, &priv->pendq);
        }

      priv->readmode = true;
      spin_unlock_irqrestore(&priv->lock, flags);
      capture_start_dma(priv);
    }

  for (; ; )
    {
      flags = spin_lock_irqsave(&priv->lock);
      apb = (FAR struct ap_buffer_s *)dq_peek(&priv->doneq);
      readmode = priv->readmode;
      spin_unlock_irqrestore(&priv->lock, flags);

      if (!readmode)
        {
          return -EAGAIN;
        }

      if (apb != NULL)
        {
          break;
        }

      /* Extra posts (stop, a buffer already consumed) only cost one more
       * trip round this loop.
       */

      ret = nxsem_tickwait(&priv->readsem,
                           MSEC2TICK(CAPTURE_READ_TIMEOUT_MS));
      if (ret == -ETIMEDOUT)
        {
          return -EAGAIN;
        }

      if (ret < 0)
        {
          return ret;
        }
    }

  /* The buffer sits in neither pendq nor the DMA while it is in doneq, so
   * it is safe to copy outside the lock.
   */

  if (apb->curbyte == 0)
    {
      up_invalidate_dcache((uintptr_t)apb->samp,
                           (uintptr_t)apb->samp + apb->nbytes);
    }

  n = MIN(buflen, (size_t)(apb->nbytes - apb->curbyte));
  memcpy(buffer, apb->samp + apb->curbyte, n);

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->readmode &&
      dq_peek(&priv->doneq) == (FAR dq_entry_t *)&apb->dq_entry)
    {
      apb->curbyte += n;
      if (apb->curbyte >= apb->nbytes)
        {
          dq_rem(&apb->dq_entry, &priv->doneq);
          apb->curbyte = 0;
          dq_addlast(&apb->dq_entry, &priv->pendq);
          requeue = true;
        }
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (requeue)
    {
      capture_start_dma(priv);
    }

  return (int)n;
}

static int capture_reserve(FAR struct audio_lowerhalf_s *dev)
{
  (void)dev;
  return OK;
}

static int capture_release(FAR struct audio_lowerhalf_s *dev)
{
  (void)dev;
  return OK;
}

int bk7258_audio_capture_initialize(void)
{
  static bool inited;
  int ret;

  if (inited)
    {
      return OK;
    }

  spin_lock_init(&g_capture.lock);
  nxsem_init(&g_capture.readsem, 0, 0);
  g_capture.dev.ops  = &g_bk7258_capture_ops;
  g_capture.channels = 1;
  g_capture.bps      = 16;
  g_capture.samprate = 16000;
  g_capture.volume   = BK7258_ADC_VOLUME_DEFAULT;

  ret = bk7258_gdma_initialize();
  if (ret < 0)
    {
      AUD_ERR("gdma_initialize failed: %d", ret);
      return ret;
    }

  g_capture.dma_ch = bk7258_gdma_alloc();
  if (g_capture.dma_ch < 0)
    {
      AUD_ERR("gdma_alloc failed: %d", g_capture.dma_ch);
      return g_capture.dma_ch;
    }

  ret = audio_register("pcm0c", &g_capture.dev);
  if (ret < 0)
    {
      AUD_ERR("audio_register(pcm0c) failed: %d", ret);
      return ret;
    }

  inited = true;
  AUD_INFO("registered /dev/audio/pcm0c");
  return OK;
}

int bk7258_audio_pa_register(FAR const struct bk7258_audio_pa_ops *ops)
{
  int ret = OK;

  /* Do not expose callbacks until the board has established a known-safe
   * PA state.  Clearing first also ensures a failed replacement leaves no
   * stale ops registered.
   */

  g_bk7258_pa_ops = NULL;

  if (ops != NULL && ops->init != NULL)
    {
      ret = ops->init(ops->arg);
      if (ret < 0)
        {
          return ret;
        }
    }

  g_bk7258_pa_ops = ops;
  return ret;
}

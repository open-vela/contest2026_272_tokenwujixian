/****************************************************************************
 * chips/bk7258/audio/bk7258_audio_capture.c
 *
 * BK7258 internal-ADC capture audio lower-half.  Composes the register-level
 * ADC HAL (bk7258_adc.c) into a NuttX audio device registered as
 * /dev/audio/pcm0c.
 *
 * Capture on BK7258 is a single analog MIC (only micen, ana_reg19[28], is
 * wired) and the GDMA block has no peripheral-to-memory channel, so the data
 * path is polled: audio_read() passes the caller's read(2) straight through
 * to the lower-half `read` callback below, which drains AUD_ADC_FPORT into
 * the supplied buffer.  Each 32-bit FIFO word packs {R:16, L:16}; the single
 * MIC (CHL_L) is the low 16 bits.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_audio.h>
#include <arch/chip/bk7258_adc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CAP_INFO(fmt, ...) audinfo("BK7258: " fmt "\n", ##__VA_ARGS__)
#define CAP_ERR(fmt, ...)  auderr("BK7258 ERROR: " fmt "\n", ##__VA_ARGS__)

/* Digital ADC gain for a single connected analog MIC.  Matches the value the
 * P1 bring-up validated (apps/testing/bk7258_pcm): 0x2D yields a healthy
 * peak-to-peak on a connected MIC without clipping, while a floating input
 * sits near full-swing.  The agent additionally applies its own software
 * capture gain (AGENT_AUDIO_CAPTURE_GAIN, default 6); lower this value if the
 * combined gain saturates on a loud input.
 */

#define BK7258_ADC_GAIN_DEFAULT 0x2Du

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_capture_s
{
  struct audio_lowerhalf_s dev;         /* Audio lower half (must be first) */

  uint32_t sample_rate;                 /* Current sample rate */
  uint8_t  channels;                    /* Current channel count */
  uint8_t  bpsamp;                      /* Bits per sample */
  bool     configured;                  /* ADC block primed */
  bool     running;                     /* Capture clock/data path active */
  bool     closing;                     /* Shutdown barrier */
  spinlock_t lock;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  bk7258_capture_getcaps(FAR struct audio_lowerhalf_s *dev,
                                   int type,
                                   FAR struct audio_caps_s *caps);
static int  bk7258_capture_configure(FAR struct audio_lowerhalf_s *dev,
                                     FAR const struct audio_caps_s *caps);
static int  bk7258_capture_start(FAR struct audio_lowerhalf_s *dev);
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int  bk7258_capture_stop(FAR struct audio_lowerhalf_s *dev);
#endif
static int  bk7258_capture_shutdown(FAR struct audio_lowerhalf_s *dev);
static int  bk7258_capture_read(FAR struct audio_lowerhalf_s *dev,
                                FAR char *buffer, size_t buflen);
static int  bk7258_capture_reserve(FAR struct audio_lowerhalf_s *dev);
static int  bk7258_capture_release(FAR struct audio_lowerhalf_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_bk7258_capture_ops =
{
  bk7258_capture_getcaps,   /* getcaps        */
  bk7258_capture_configure, /* configure      */
  bk7258_capture_shutdown,  /* shutdown       */
  bk7258_capture_start,     /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  bk7258_capture_stop,      /* stop           */
#endif
  NULL,                     /* pause          */
  NULL,                     /* resume         */
  NULL,                     /* allocbuffer    */
  NULL,                     /* freebuffer     */
  NULL,                     /* enqueuebuffer  */
  NULL,                     /* cancelbuffer   */
  NULL,                     /* ioctl          */
  bk7258_capture_read,      /* read           */
  NULL,                     /* write          */
  bk7258_capture_reserve,   /* reserve        */
  bk7258_capture_release    /* release        */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_capture_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
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
        caps->ac_channels = AUDIO_CHANNELS_RANGE(1, 1);
        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] = AUDIO_TYPE_INPUT;
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
        caps->ac_channels = AUDIO_CHANNELS_RANGE(1, 1);
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            /* 16 kHz mono — the rate the ASR path requires. */
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_16K;
          }
        break;

      default:
        caps->ac_subtype  = 0;
        caps->ac_channels = 0;
        break;
    }

  return caps->ac_len;
}

static int bk7258_capture_configure(FAR struct audio_lowerhalf_s *dev,
                                    FAR const struct audio_caps_s *caps)
{
  FAR struct bk7258_capture_s *priv =
      (FAR struct bk7258_capture_s *)dev;
  struct bk7258_adc_cfg_s adc_cfg;
  irqstate_t flags;
  uint32_t sample_rate;
  uint8_t  bpsamp;
  uint8_t  channels;
  int ret;

  if (caps->ac_type != AUDIO_TYPE_INPUT)
    {
      return -EINVAL;
    }

  sample_rate = caps->ac_controls.hw[0]
              | ((uint32_t)caps->ac_controls.b[3] << 16);
  channels = caps->ac_channels;
  bpsamp   = caps->ac_controls.b[2];

  if (sample_rate == 0)
    {
      sample_rate = BK7258_AUDIO_DEFAULT_SAMPLE_RATE;
    }

  if (channels == 0)
    {
      channels = 1;
    }

  if (bpsamp == 0)
    {
      bpsamp = BK7258_AUDIO_DEFAULT_BPSAMP;
    }

  if (bpsamp != 16)
    {
      CAP_ERR("only S16_LE supported (asked for %u)", bpsamp);
      return -EINVAL;
    }

  if (channels != 1)
    {
      CAP_ERR("only mono supported (asked for %u)", channels);
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ESHUTDOWN;
    }

  priv->sample_rate = sample_rate;
  priv->channels    = channels;
  priv->bpsamp      = bpsamp;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Program the ADC block.  Single analog MIC on the left input; the clock
   * mux is shared with the DAC and bk7258_adc_configure self-invokes
   * bk7258_aud_subsystem_init().
   */

  adc_cfg.samp_rate = sample_rate;
  adc_cfg.channel   = BK7258_ADC_CHL_L;
  adc_cfg.gain      = BK7258_ADC_GAIN_DEFAULT;
  adc_cfg.samp_edge = false;
  adc_cfg.use_apll  = true;

  ret = bk7258_adc_configure(&adc_cfg);
  if (ret < 0)
    {
      CAP_ERR("adc_configure failed: %d", ret);
      return ret;
    }

  priv->configured = true;
  return OK;
}

static int bk7258_capture_start(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ESHUTDOWN;
    }

  if (!priv->configured)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EINVAL;
    }

  if (priv->running)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  priv->running = true;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Enable the ADC clock and data path.  From here the FIFO fills at the
   * configured sample rate; the read callback drains it.
   */

  bk7258_adc_start();
  return OK;
}

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int bk7258_capture_stop(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->closing)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ESHUTDOWN;
    }

  priv->running = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  bk7258_adc_stop();
  return OK;
}
#endif

static int bk7258_capture_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  priv->closing = true;
  priv->running = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (priv->configured)
    {
      /* Stops the capture clock, drops MIC enables and restores the digital
       * defaults (subsystem clock/power stays up).
       */

      bk7258_adc_deinit();
    }
  else
    {
      bk7258_adc_stop();
    }

  /* Lift the barrier so the registered lower half is reusable by a later
   * open.
   */

  flags = spin_lock_irqsave(&priv->lock);
  priv->configured = false;
  priv->closing    = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  return OK;
}

static int bk7258_capture_read(FAR struct audio_lowerhalf_s *dev,
                               FAR char *buffer, size_t buflen)
{
  FAR struct bk7258_capture_s *priv = (FAR struct bk7258_capture_s *)dev;
  FAR int16_t *dst = (FAR int16_t *)buffer;
  size_t nsamples = buflen / sizeof(int16_t);
  size_t i;
  irqstate_t flags;

  if ((buflen & 1u) != 0u || nsamples == 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->running)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EPIPE;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  for (i = 0; i < nsamples; i++)
    {
      uint32_t word;

      if (bk7258_adc_fifo_empty())
        {
          break;
        }

      word   = bk7258_adc_fifo_read();
      dst[i] = (int16_t)(word & UINT32_C(0xFFFF));
    }

  if (i == 0)
    {
      return -EAGAIN;
    }

  return (int)(i * sizeof(int16_t));
}

static int bk7258_capture_reserve(FAR struct audio_lowerhalf_s *dev)
{
  (void)dev;
  return OK;
}

static int bk7258_capture_release(FAR struct audio_lowerhalf_s *dev)
{
  (void)dev;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_audio_capture_initialize(void)
{
  FAR struct bk7258_capture_s *priv;
  int ret;

  priv = kmm_zalloc(sizeof(struct bk7258_capture_s));
  if (priv == NULL)
    {
      CAP_ERR("alloc failed");
      return -ENOMEM;
    }

  priv->dev.ops     = &g_bk7258_capture_ops;
  priv->sample_rate = BK7258_AUDIO_DEFAULT_SAMPLE_RATE;
  priv->channels    = 1;
  priv->bpsamp      = BK7258_AUDIO_DEFAULT_BPSAMP;
  spin_lock_init(&priv->lock);

  ret = audio_register("pcm0c", &priv->dev);
  if (ret < 0)
    {
      CAP_ERR("audio_register failed: %d", ret);
      kmm_free(priv);
      return ret;
    }

  CAP_INFO("registered /dev/audio/pcm0c");
  return OK;
}

/****************************************************************************
 * drivers/audio/bk7258_audio.h
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

#ifndef __CHIPS_BK7258_AUDIO_BK7258_AUDIO_H
#define __CHIPS_BK7258_AUDIO_BK7258_AUDIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>
#include <nuttx/queue.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default configuration ***************************************************/

#define BK7258_AUDIO_DEFAULT_SAMPLE_RATE  16000
#define BK7258_AUDIO_DEFAULT_CHANNELS     2
#define BK7258_AUDIO_DEFAULT_BPSAMP       16
#define BK7258_AUDIO_DEFAULT_VOLUME       20    /* 0..100                   */
#define BK7258_AUDIO_MAX_DAC_GAIN         0x2Du /* AVDK default, ~0 dB.     */

/* Power Amplifier control ************************************************
 *
 * PA GPIO, active level and settling delays are board-specific and MUST NOT
 * be hard coded in the chip-level driver.  The board layer implements
 * struct bk7258_audio_pa_ops and registers it via
 * bk7258_audio_pa_register() before playback starts.
 */

/* Buffer configuration ****************************************************/

#define BK7258_AUDIO_NUM_BUFFERS          4
#define BK7258_AUDIO_BUFFER_SIZE          4096

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Device type: playback or capture */

typedef enum
{
  BK7258_AUDIO_TYPE_PLAYBACK = 0,
  BK7258_AUDIO_TYPE_CAPTURE
} bk7258_audio_type_e;

/* Private data structure for BK7258 audio device */

struct bk7258_audio_s
{
  struct audio_lowerhalf_s dev;         /* Audio lower half (must be first) */

  /* Device configuration */

  bk7258_audio_type_e type;             /* Playback or capture */
  uint32_t sample_rate;                 /* Current sample rate */
  uint8_t channels;                     /* Current channel count */
  uint8_t bpsamp;                       /* Bits per sample */
  uint8_t  volume;                      /* 0..100                          */
  bool     mute;                        /* True while mute is asserted     */
  bool     configured;                  /* DAC hardware primed             */
  bool     dac_running;                 /* Run gate for IRQ/enqueue starts  */
  bool     dma_active;                  /* pendq head is programmed in DMA  */
  bool     closing;                     /* Temporary shutdown barrier       */
  bool     stopping;                    /* Stop rejects callback refills    */
  bool     faulted;                     /* Fault gate during error teardown */
  bool     fault_pending;               /* LPWORK must make hardware safe   */
  bool     recover_after_fault;         /* Clear fault after failq drains   */
  bool     complete_pending;            /* LPWORK owes a COMPLETE callback  */
  bool     underrun_pending;            /* LPWORK owes an underrun callback */
  bool     final_seen;                  /* AUDIO_APB_FINAL on last buffer  */
  int      dma_error;                   /* Persistent asynchronous error    */

  /* Buffer queues.
   *
   * pendq: buffers owned by the driver; dma_active says whether its head is
   *        currently being transferred.
   * doneq: buffers finished by the DMA IRQ, waiting for successful dequeue.
   * failq: buffers affected by an asynchronous DMA fault, waiting for an
   *        error dequeue after LPWORK has made the hardware safe.
   */

  dq_queue_t pendq;
  dq_queue_t doneq;
  dq_queue_t failq;

  /* GDMA channel handle. -1 while none is allocated. */

  int dma_ch;

  spinlock_t lock;
  struct work_s work;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Board-provided Power Amplifier (PA) control ops.
 *
 * The chip-level BK7258 audio driver never touches PA GPIOs directly.  The
 * board bring-up code registers a struct bk7258_audio_pa_ops that owns the
 * physical GPIO, the active level and the required settling delays so the
 * same driver works across different HW variants (e.g. HT6873 today, a
 * different PA tomorrow).
 *
 * All callbacks may be NULL, in which case the driver silently skips the
 * corresponding step.
 */

struct bk7258_audio_pa_ops
{
  /* Configure the PA GPIO as output, drive the "off" level. */

  CODE int  (*init)(FAR void *arg);

  /* Enable the PA.  The board is responsible for the on-settling delay
   * (typically tens of ms) before returning so that the driver can safely
   * un-mute the DAC right after.  A negated errno leaves the PA disabled.
   */

  CODE int  (*enable)(FAR void *arg);

  /* Disable the PA.  Board is responsible for the off-settling delay after
   * driving the pin so the DAC can be shut down cleanly on return.  It must
   * drive the PA inactive even if a settling delay fails.
   */

  CODE int  (*disable)(FAR void *arg);

  FAR void *arg;
};

/****************************************************************************
 * Name: bk7258_audio_pa_register
 *
 * Description:
 *   Register the board's PA control ops.  Must be called before playback is
 *   started.  Passing NULL clears the registration.  The ops are retained
 *   only when init succeeds.
 *
 * Returned Value:
 *   The board init callback's return value, or zero when no init callback is
 *   provided.  A negated errno value indicates failure.
 *
 ****************************************************************************/

int bk7258_audio_pa_register(FAR const struct bk7258_audio_pa_ops *ops);

/****************************************************************************
 * Name: bk7258_audio_playback_initialize
 *
 * Description:
 *   Initialize and register the BK7258 playback audio device as
 *   /dev/audio/pcm0p
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_audio_playback_initialize(void);

/****************************************************************************
 * Name: bk7258_audio_capture_initialize
 *
 * Description:
 *   Initialize and register the BK7258 capture audio device as
 *   /dev/audio/pcm0c
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_audio_capture_initialize(void);

#endif /* __CHIPS_BK7258_AUDIO_BK7258_AUDIO_H */

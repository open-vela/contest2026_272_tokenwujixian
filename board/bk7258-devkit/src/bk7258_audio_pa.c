/****************************************************************************
 * board/bk7258-devkit/src/bk7258_audio_pa.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version
 * 2.0 (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <syslog.h>

#include <arch/chip/bk7258_audio.h>
#include <nuttx/clock.h>
#include <nuttx/signal.h>

#include "bk7258_internal.h"
#include "board.h"

#ifdef CONFIG_AUDIO_BK7258

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_devkit_pa_delay(unsigned int delay_ms)
{
  int ret;

  /* Restart the complete short delay after a signal interruption.  This may
   * wait longer than requested, but never shortens the PA settling time.
   */

  do
    {
      ret = nxsig_usleep(delay_ms * 1000);
    }
  while (ret == -EINTR);

  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 PA: delay failed: %d\n", ret);
    }

  return ret;
}

static void bk7258_devkit_pa_write(bool enable)
{
#if BOARD_AUDIO_PA_ACTIVE_HIGH
  bk7258_gpio_write(BOARD_AUDIO_PA_PIN, enable);
#else
  bk7258_gpio_write(BOARD_AUDIO_PA_PIN, !enable);
#endif
}

static int bk7258_devkit_pa_init(FAR void *arg)
{
  int ret;

  (void)arg;

  /* Configure PA pin as output, initially low (PA disabled) */

  ret = bk7258_gpio_config_output(BOARD_AUDIO_PA_PIN,
                                  !BOARD_AUDIO_PA_ACTIVE_HIGH);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 PA: failed to configure GPIO%d: %d\n",
             BOARD_AUDIO_PA_PIN, ret);
      return ret;
    }

  syslog(LOG_INFO, "BK7258 PA: initialized GPIO%d (active-%s)\n",
         BOARD_AUDIO_PA_PIN,
         BOARD_AUDIO_PA_ACTIVE_HIGH ? "high" : "low");

  return OK;
}

static int bk7258_devkit_pa_enable(FAR void *arg)
{
  int ret;

  (void)arg;

  /* Keep the PA inactive unless both settling delays succeed. */

  ret = bk7258_devkit_pa_delay(BOARD_AUDIO_PA_PRE_ENABLE_MS);
  if (ret < 0)
    {
      bk7258_devkit_pa_write(false);
      return ret;
    }

  bk7258_devkit_pa_write(true);

  ret = bk7258_devkit_pa_delay(BOARD_AUDIO_PA_POST_ENABLE_MS);
  if (ret < 0)
    {
      bk7258_devkit_pa_write(false);
      return ret;
    }

  syslog(LOG_DEBUG, "BK7258 PA: enabled\n");
  return OK;
}

static int bk7258_devkit_pa_disable(FAR void *arg)
{
  int post_ret;
  int pre_ret;

  (void)arg;

  /* Delay failures are diagnostic only.  Once this callback returns, the
   * safety-critical guarantee is that the PA GPIO has been driven inactive.
   */

  pre_ret = bk7258_devkit_pa_delay(BOARD_AUDIO_PA_PRE_DISABLE_MS);
  bk7258_devkit_pa_write(false);
  post_ret = bk7258_devkit_pa_delay(BOARD_AUDIO_PA_POST_DISABLE_MS);

  if (pre_ret < 0 || post_ret < 0)
    {
      syslog(LOG_ERR,
             "BK7258 PA: disable delay failed (pre=%d post=%d)\n",
             pre_ret, post_ret);
    }

  syslog(LOG_DEBUG, "BK7258 PA: disabled\n");
  return OK;
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bk7258_audio_pa_ops g_bk7258_devkit_pa_ops =
{
  .init    = bk7258_devkit_pa_init,
  .enable  = bk7258_devkit_pa_enable,
  .disable = bk7258_devkit_pa_disable,
  .arg     = NULL,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_devkit_audio_pa_register(void)
{
  return bk7258_audio_pa_register(&g_bk7258_devkit_pa_ops);
}

#endif /* CONFIG_AUDIO_BK7258 */

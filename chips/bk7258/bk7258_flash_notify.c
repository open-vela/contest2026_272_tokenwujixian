/****************************************************************************
 * chips/bk7258/bk7258_flash_notify.c
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

/****************************************************************************
 * Flash operation notification between the two cores
 *
 * Every flash operation is bracketed by mb_flash_op_prepare() and
 * mb_flash_op_finish(), because the AP shares this controller: while the CP
 * is erasing or programming, the AP must not touch it either.  The bracket
 * sends a START/END command over the flash mailbox channel and waits for the
 * peer's acknowledgement, with a 5 ms ceiling.
 *
 * Two things about this file are deliberate and should not be "tidied up":
 *
 *   - The wait loop is bounded twice -- 2000 iterations and 5 ms of wall clock
 *     -- and it returns BK_OK when the loop simply runs out.  That is what the
 *     authority does, and it is what the callers' best-effort contract expects.
 *     Turning the timeout into an error would turn a slow peer into a failed
 *     flash write.
 *   - The microsecond clock is bk_aon_rtc_get_us(), not a NuttX tick.  The
 *     window is 5 ms; the system tick is 1 ms and would be too coarse.  The
 *     always-on RTC is also the only counter that survives sleep, which is
 *     when this path matters most.
 *
 * With CONFIG_FLASH_MB off the bracket degrades to "tell the local consumer to
 * stop and start again", through the callback registered by
 * mb_flash_register_op_notify().
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <common/bk_include.h>
#include <driver/flash.h>
#include <os/os.h>

#include "flash_driver.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Consumer registered by the local side when CONFIG_FLASH_MB is off. */

static void (*g_flash_op_notify)(uint32_t param);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bk_err_t mb_flash_register_op_notify(void *notify_cb)
{
  g_flash_op_notify = (void (*)(uint32_t))notify_cb;
  return BK_OK;
}

bk_err_t mb_flash_unregister_op_notify(void *notify_cb)
{
  if (g_flash_op_notify == notify_cb)
    {
      g_flash_op_notify = NULL;
      return BK_OK;
    }

  return BK_ERR_FLASH_WAIT_CB_NOT_REGISTER;
}

#if CONFIG_FLASH_MB

/****************************************************************************
 * Included Files (mailbox channel lives behind this switch)
 ****************************************************************************/

#include <driver/mailbox_channel.h>

#if CONFIG_CACHE_ENABLE
#include "cache.h"
#endif

#include <driver/aon_rtc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Ceiling for one START/END round trip, in microseconds. */

#define FLASH_WAIT_ACK_TIMEOUT_US  5000

/* Bounded spin count, so a peer that never acknowledges cannot hang us even if
 * the always-on counter misbehaves.
 */

#define FLASH_WAIT_ACK_SPINS       2000

#define IPC_FLASH_OP_COMPLETE      0
#define IPC_FLASH_OP_REQ           1
#define IPC_FLASH_OP_ACK           2

#define IPC_FLASH_OP_START         0
#define IPC_FLASH_OP_END           1

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Latest state reported by the peer's acknowledgement. */

static volatile uint32_t g_flash_erase_ipc_state = IPC_FLASH_OP_COMPLETE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bk_err_t bk7258_flash_send_op_state(uint8_t state)
{
  mb_chnl_cmd_t cmd_buf;
  uint64_t      us_start = 0;
  uint64_t      us_end   = 0;
  bk_err_t      ret;

  g_flash_erase_ipc_state = IPC_FLASH_OP_REQ;

  cmd_buf.hdr.data = 0;                 /* clear hdr */
  cmd_buf.hdr.cmd  = state;
  cmd_buf.param1   = g_flash_erase_ipc_state;

  ret = mb_chnl_write(MB_CHNL_FLASH, &cmd_buf);
  if (ret != BK_OK)
    {
      return ret;
    }

  us_start = bk_aon_rtc_get_us();

  for (int i = 0; i < FLASH_WAIT_ACK_SPINS; i++)
    {
      if (g_flash_erase_ipc_state == IPC_FLASH_OP_ACK)
        {
          break;
        }

      us_end = bk_aon_rtc_get_us();

      /* waiting for the ack must not take longer than 5 ms */

      if ((us_end - us_start) > FLASH_WAIT_ACK_TIMEOUT_US)
        {
          return BK_FAIL;
        }
    }

  return BK_OK;
}

/* Called from the mailbox channel's TX-complete interrupt. */

static void bk7258_flash_ipc_tx_cmpl_isr(void *chn_param,
                                         mb_chnl_ack_t *ack_buf)
{
  if ((ack_buf->hdr.state & CHNL_STATE_COM_FAIL) == 0)
    {
      g_flash_erase_ipc_state = ack_buf->ack_data1;
    }
}

/* The peer never sends commands in this direction; the channel requires a
 * handler, so this one does nothing.
 */

static void bk7258_flash_ipc_rx_isr(void *chn_param,
                                    mb_chnl_cmd_t *cmd_buf)
{
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bk_err_t mb_flash_op_prepare(void)
{
  bk_err_t ret = BK_OK;

#if (CONFIG_CPU_CNT > 1)
  extern int mb_ipc_cpu_is_power_off(uint32_t cpu_id);

  if (mb_ipc_cpu_is_power_off(1))       /* CPU1 is down: nobody to tell */
    {
      return BK_OK;
    }

  ret = bk7258_flash_send_op_state(IPC_FLASH_OP_START);
#endif

  return ret;
}

bk_err_t mb_flash_op_finish(void)
{
  bk_err_t ret = BK_OK;

#if (CONFIG_CPU_CNT > 1)
  extern int mb_ipc_cpu_is_power_off(uint32_t cpu_id);

  if (mb_ipc_cpu_is_power_off(1))
    {
      return BK_OK;
    }

  ret = bk7258_flash_send_op_state(IPC_FLASH_OP_END);
#endif

  return ret;
}

bk_err_t mb_flash_ipc_init(void)
{
  bk_err_t ret = mb_chnl_open(MB_CHNL_FLASH, NULL);

  if (ret != BK_OK)
    {
      return ret;
    }

  mb_chnl_ctrl(MB_CHNL_FLASH, MB_CHNL_SET_TX_CMPL_ISR,
               (void *)bk7258_flash_ipc_tx_cmpl_isr);
  mb_chnl_ctrl(MB_CHNL_FLASH, MB_CHNL_SET_RX_ISR,
               (void *)bk7258_flash_ipc_rx_isr);

  return ret;
}

#else /* !CONFIG_FLASH_MB */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* No cross-core channel: bracket the local consumer instead -- tell it to stop
 * touching the controller, then let it resume.
 */

bk_err_t mb_flash_ipc_init(void)
{
  return BK_OK;
}

bk_err_t mb_flash_op_prepare(void)
{
  if (g_flash_op_notify != NULL)
    {
      g_flash_op_notify(0);
    }

  return BK_OK;
}

bk_err_t mb_flash_op_finish(void)
{
  if (g_flash_op_notify != NULL)
    {
      g_flash_op_notify(1);
    }

  return BK_OK;
}

#endif /* CONFIG_FLASH_MB */

/****************************************************************************
 * Flash operate status
 *
 * Both live in .itcm_sec_code: a flash operation must be able to ask what state
 * the driver is in while the flash controller is otherwise busy.
 ****************************************************************************/

static volatile flash_op_status_t g_flash_op_status;

__attribute__((section(".itcm_sec_code")))
bk_err_t bk_flash_set_operate_status(flash_op_status_t status)
{
  g_flash_op_status = status;
  return BK_OK;
}

__attribute__((section(".itcm_sec_code")))
flash_op_status_t bk_flash_get_operate_status(void)
{
  return g_flash_op_status;
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

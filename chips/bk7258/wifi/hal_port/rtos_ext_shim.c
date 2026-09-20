/*
 * Narrow Armino event/HISR adapter.
 *
 * This file deliberately owns only rtos_ext.h. Threads, queues, locks,
 * semaphores, and time remain implemented by bk7258_wifi_osal.c; the legacy
 * rtos_shim.c is not part of this build.
 */

#include <nuttx/config.h>
#include <nuttx/wqueue.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "bk7258_wifi_internal.h"
#include "os/rtos_ext.h"

#define BK7258_HISR_SLOTS 8

struct bk7258_hisr_slot_s
{
  rtos_hisr_cb_t callback;
  struct work_s work;
  bool pending;
};

static struct bk7258_hisr_slot_s g_hisr_slots[BK7258_HISR_SLOTS];

static struct bk7258_hisr_slot_s *bk7258_hisr_find(rtos_hisr_cb_t callback)
{
  unsigned int i;

  for (i = 0; i < BK7258_HISR_SLOTS; i++)
    {
      if (g_hisr_slots[i].callback == callback)
        {
          return &g_hisr_slots[i];
        }
    }

  return NULL;
}

static void bk7258_hisr_worker(FAR void *arg)
{
  FAR struct bk7258_hisr_slot_s *slot = arg;
  rtos_hisr_cb_t callback = slot->callback;

  slot->pending = false;
  if (callback != NULL)
    {
      callback();
    }
}

bk_err_t rtos_init_event_ex(rtos_event_ext_t *event_ext)
{
  int ret;

  if (event_ext == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  event_ext->event_flag = 0;
  ret = bk7258_wifi_osal_sem_create(
      (uintptr_t *)&event_ext->event_semaphore, 1);
  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t deinit_event_ex(rtos_event_ext_t *event_ext)
{
  if (event_ext == NULL || event_ext->event_semaphore == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  bk7258_wifi_osal_sem_delete((uintptr_t)event_ext->event_semaphore);
  event_ext->event_semaphore = NULL;
  event_ext->event_flag = 0;
  return BK_OK;
}

uint32_t set_event_ex(rtos_event_ext_t *event_ext, uint32_t event)
{
  uint32_t flags;

  if (event_ext == NULL || event_ext->event_semaphore == NULL)
    {
      return 0;
    }

  flags = bk7258_wifi_osal_enter_critical();
  event_ext->event_flag |= event;
  uint32_t result = event_ext->event_flag;
  bk7258_wifi_osal_exit_critical(flags);
  bk7258_wifi_osal_sem_post((uintptr_t)event_ext->event_semaphore);
  return result;
}

uint32_t wait_event_ex(rtos_event_ext_t *event_ext, uint32_t event,
                       uint32_t timeout_ms)
{
  uint32_t matched;
  uint64_t deadline = 0;
  int ret;

  if (event_ext == NULL || event_ext->event_semaphore == NULL)
    {
      return 0;
    }

  if (timeout_ms != BEKEN_WAIT_FOREVER && timeout_ms != 0)
    {
      deadline = bk7258_wifi_osal_time_ms() + timeout_ms;
    }

  for (;;)
    {
      uint32_t critical = bk7258_wifi_osal_enter_critical();
      matched = event_ext->event_flag & event;
      if (matched != 0)
        {
          event_ext->event_flag &= ~matched;
        }
      bk7258_wifi_osal_exit_critical(critical);
      if (matched != 0)
        {
          return matched;
        }

      if (timeout_ms == 0)
        {
          ret = bk7258_wifi_osal_sem_wait(
              (uintptr_t)event_ext->event_semaphore, 0);
        }
      else if (timeout_ms == BEKEN_WAIT_FOREVER)
        {
          ret = bk7258_wifi_osal_sem_wait(
              (uintptr_t)event_ext->event_semaphore, BEKEN_WAIT_FOREVER);
        }
      else
        {
          uint64_t now = bk7258_wifi_osal_time_ms();
          uint64_t remaining = now >= deadline ? 0 : deadline - now;
          ret = bk7258_wifi_osal_sem_wait(
              (uintptr_t)event_ext->event_semaphore,
              remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining);
        }
      if (ret < 0)
        {
          return 0;
        }
    }
}

bk_err_t rtos_create_hisr(rtos_hisr_cb_t callback, int8_t priority,
                          char *name)
{
  unsigned int i;

  (void)priority;
  (void)name;
  if (callback == NULL || bk7258_hisr_find(callback) != NULL)
    {
      return BK_ERR_PARAM;
    }

  for (i = 0; i < BK7258_HISR_SLOTS; i++)
    {
      if (g_hisr_slots[i].callback == NULL)
        {
          g_hisr_slots[i].callback = callback;
          g_hisr_slots[i].pending = false;
          return BK_OK;
        }
    }

  return BK_ERR_NO_MEM;
}

void rtos_activate_hisr(rtos_hisr_cb_t callback, int8_t priority)
{
  struct bk7258_hisr_slot_s *slot;

  (void)priority;
  slot = bk7258_hisr_find(callback);
  if (slot == NULL || slot->pending)
    {
      return;
    }

  slot->pending = true;
  if (work_queue(HPWORK, &slot->work, bk7258_hisr_worker, slot, 0) < 0)
    {
      slot->pending = false;
    }
}

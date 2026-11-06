/****************************************************************************
 * chips/bk7258/bk7258_drv_model.c
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
 * Armino device model
 *
 * The 12 symbols of middleware/driver/common/drv_model.c: a table of
 * DD_MAX_DEV slots, each holding a state, a use count and a pointer to the
 * device's DD_OPERATIONS, plus the open/close/read/write/control dispatch
 * that goes through it.
 *
 * The handle is the slot index with a magic word in the top half
 * (DD_HANDLE_MAGIC_WORD 0xA5A50000), so every entry point starts by
 * validating that magic and the index before touching the table.
 *
 * Critical sections are NuttX's up_irq_save()/up_irq_restore() pair rather
 * than the authority's GLOBAL_INT_* macros: same save-and-restore shape, but
 * the IRQ state is a NuttX primitive instead of a wrapper over the Armino RTOS
 * seam.  The state and use-count updates stay inside one section, which is what
 * keeps an ISR from landing between them.
 *
 * ddev_control() carries one extra guard the others do not have: after the
 * handle check it re-tests idx against DD_MAX_DEV.  It is redundant with
 * ddev_get_id_from_handle()'s DD_DEV_TYPE_END bound, but the authority has it
 * and a device whose type sits between DD_DEV_TYPE_END and DD_MAX_DEV would
 * read past the table without it, so it stays.
 *
 * Live symbols here are drv_model_init, ddev_get_id_from_handle and
 * ddev_control.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <string.h>

#include <nuttx/irq.h>

#include "bk_drv_model.h"
#include "drv_model.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DRV_DEV_S drv_dev_tbl[DD_MAX_DEV] =
{
  {0},
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

UINT32 drv_model_init(void)
{
  memset(drv_dev_tbl, 0, sizeof(drv_dev_tbl));

  return DRV_SUCCESS;
}

UINT32 drv_model_uninit(void)
{
  memset(drv_dev_tbl, 0, sizeof(drv_dev_tbl));

  return DRV_SUCCESS;
}

UINT32 ddev_check_handle(DD_HANDLE handle)
{
  UINT32 magic;
  UINT32 idx;

  magic = handle & DD_HANDLE_MAGIC_MASK;
  idx   = handle & DD_HANDLE_ID_MASK;
  if (DD_HANDLE_MAGIC_WORD == magic && idx < DD_MAX_DEV)
    {
      return DRV_SUCCESS;
    }
  else
    {
      return DRV_FAILURE;
    }
}

DD_HANDLE ddev_make_handle(UINT32 idx)
{
  UINT32 handle = DD_HANDLE_UNVALID;

  if (idx >= DD_MAX_DEV)
    {
      goto make_exit;
    }

  handle = idx + DD_HANDLE_MAGIC_WORD;

make_exit:
  return handle;
}

UINT32 ddev_get_id_from_handle(DD_HANDLE handle)
{
  UINT32 magic;
  UINT32 idx;

  magic = handle & DD_HANDLE_MAGIC_MASK;
  idx   = handle & DD_HANDLE_ID_MASK;

  if (magic != DD_HANDLE_MAGIC_WORD || idx >= DD_DEV_TYPE_END)
    {
      return DD_ID_UNVALID;
    }

  return idx;
}

DD_HANDLE ddev_open(dd_device_type dev, UINT32 *status, UINT32 op_flag)
{
  UINT32 idx;
  UINT32 handle;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  irqstate_t flags;

  handle = DD_HANDLE_UNVALID;
  idx = ddev_get_id_from_handle(dev);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  *status = DRV_FAILURE;

  dev_ptr = &drv_dev_tbl[idx];
  if (DD_STATE_OPENED == dev_ptr->state)
    {
      handle = ddev_make_handle(idx);
    }
  else if (DD_STATE_CLOSED == dev_ptr->state)
    {
      handle = ddev_make_handle(idx);

      operation = dev_ptr->op;
      if (operation && operation->open)
        {
          *status = (operation->open)(op_flag);
        }

      flags = up_irq_save();
      dev_ptr->state = DD_STATE_OPENED;
      dev_ptr->use_cnt = 0;
      up_irq_restore(flags);
    }
  else
    {
    }

  flags = up_irq_save();
  dev_ptr->use_cnt++;
  up_irq_restore(flags);

  return handle;
}

UINT32 ddev_close(DD_HANDLE handle)
{
  UINT32 idx;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  irqstate_t flags;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  dev_ptr = &drv_dev_tbl[idx];

  flags = up_irq_save();
  dev_ptr->use_cnt--;
  up_irq_restore(flags);

  if (0 == dev_ptr->use_cnt)
    {
      operation = dev_ptr->op;
      if (operation && operation->close)
        {
          (operation->close)();
        }

      flags = up_irq_save();
      dev_ptr->state = DD_STATE_CLOSED;
      up_irq_restore(flags);
    }

  return DRV_SUCCESS;
}

UINT32 ddev_read(DD_HANDLE handle, char *user_buf, UINT32 count,
                 UINT32 op_flag)
{
  UINT32 idx;
  UINT32 status;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  status = DRV_FAILURE;
  dev_ptr = &drv_dev_tbl[idx];
  operation = dev_ptr->op;
  if (operation && operation->read)
    {
      status = (operation->read)(user_buf, count, op_flag);
    }

  return status;
}

UINT32 ddev_write(DD_HANDLE handle, char *user_buf, UINT32 count,
                  UINT32 op_flag)
{
  UINT32 idx;
  UINT32 status;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  status    = DRV_FAILURE;
  dev_ptr   = &drv_dev_tbl[idx];
  operation = dev_ptr->op;
  if (operation && operation->write)
    {
      status = (operation->write)(user_buf, count, op_flag);
    }

  return status;
}

UINT32 ddev_control(DD_HANDLE handle, UINT32 cmd, VOID *param)
{
  UINT32 idx;
  UINT32 status;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  if (idx >= DD_MAX_DEV)
    {
      return DRV_FAILURE;
    }

  status    = DRV_FAILURE;
  dev_ptr   = &drv_dev_tbl[idx];
  operation = dev_ptr->op;
  if (operation && operation->control)
    {
      status = (operation->control)(cmd, param);
    }

  return status;
}

UINT32 ddev_register_dev(DD_HANDLE handle, DD_OPERATIONS *optr)
{
  UINT32 idx;
  DRV_DEV_PTR dev_ptr;

  if (!optr)
    {
      return DRV_FAILURE;
    }

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID != idx)
    {
      dev_ptr = &drv_dev_tbl[idx];
      if (DD_STATE_NODEVICE == dev_ptr->state)
        {
          dev_ptr->op    = optr;
          dev_ptr->state = DD_STATE_CLOSED;
        }
      else
        {
          return DRV_FAILURE;
        }
    }
  else
    {
      return DRV_FAILURE;
    }

  return DRV_SUCCESS;
}

UINT32 ddev_unregister_dev(DD_HANDLE handle)
{
  UINT32 idx;
  DRV_DEV_PTR dev_ptr;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID != idx)
    {
      dev_ptr = &drv_dev_tbl[idx];
      dev_ptr->op    = 0;
      dev_ptr->state = DD_STATE_NODEVICE;
    }

  return DRV_SUCCESS;
}

/****************************************************************************
 * Probe hook (see bk7258_probe.h)
 ****************************************************************************/

#include <stdio.h>

#include "bk7258_probe.h"

/* Walk the table the way a driver would: build handles, check them, register a
 * device, dispatch through it, unregister.  The return codes are the contract
 * (DRV_SUCCESS / DRV_FAILURE / DD_ID_UNVALID), so printing them says whether the
 * table logic is doing what the code says, which a clean boot cannot show.
 */

static DD_OPERATIONS s_probe_ops;
static int s_probe_open_calls;
static int s_probe_control_calls;
static uint32_t s_probe_last_cmd;

static UINT32 probe_open(UINT32 op_flag)
{
  (void)op_flag;
  s_probe_open_calls++;
  return DRV_SUCCESS;
}

static UINT32 probe_control(UINT32 cmd, void *param)
{
  (void)param;
  s_probe_control_calls++;
  s_probe_last_cmd = cmd;
  return DRV_SUCCESS;
}

void bk7258_drv_model_probe(void)
{
  /* ddev_make_handle() takes the table index, so the device type has to go
   * through ddev_get_id_from_handle() first -- handing it the enum value
   * (0xA5A50000 + idx) is out of range and comes back DD_HANDLE_UNVALID.
   */

  UINT32 uart1_id = ddev_get_id_from_handle(DD_DEV_TYPE_UART1);
  DD_HANDLE good = ddev_make_handle(uart1_id);
  DD_HANDLE bad = ddev_make_handle(DD_MAX_DEV);
  DD_HANDLE raw = DD_DEV_TYPE_UART1;  /* the type's own magic is already right */
  UINT32 status = 0;
  UINT32 opened;
  UINT32 closed;
  UINT32 registered;
  UINT32 unregistered;

  drv_model_init();

  printf("model: id_of(UART1)=%lu make(id)=0x%lx make(DD_MAX_DEV)=0x%lx "
         "(DD_MAX_DEV=%lu DD_HANDLE_UNVALID=0x%lx)\n",
         (unsigned long)uart1_id, (unsigned long)good, (unsigned long)bad,
         (unsigned long)DD_MAX_DEV, (unsigned long)DD_HANDLE_UNVALID);
  printf("model: check(good)=%lu check(bad)=%lu check(raw)=%lu\n",
         (unsigned long)ddev_check_handle(good),
         (unsigned long)ddev_check_handle(bad),
         (unsigned long)ddev_check_handle(raw));
  printf("model: id(good)=%lu id(raw)=%lu (DD_ID_UNVALID=%lu)\n",
         (unsigned long)ddev_get_id_from_handle(good),
         (unsigned long)ddev_get_id_from_handle(raw),
         (unsigned long)DD_ID_UNVALID);

  /* Dispatching to an unregistered slot must fail, not crash. */

  printf("model: control(before register)=%lu\n",
         (unsigned long)ddev_control(good, 0x1234, NULL));

  s_probe_ops.open = probe_open;
  s_probe_ops.control = probe_control;

  registered = ddev_register_dev(good, &s_probe_ops);
  opened = ddev_open(DD_DEV_TYPE_UART1, &status, 0);
  (void)ddev_control(good, 0x5678, NULL);
  closed = ddev_close(good);
  unregistered = ddev_unregister_dev(good);

  printf("model: register=%lu open_status=%lu open_handle=0x%lx "
         "open_calls=%d control_calls=%d last_cmd=0x%lx\n",
         (unsigned long)registered, (unsigned long)status,
         (unsigned long)opened, s_probe_open_calls, s_probe_control_calls,
         (unsigned long)s_probe_last_cmd);
  printf("model: close=%lu unregister=%lu dispatch(after)=%lu\n",
         (unsigned long)closed, (unsigned long)unregistered,
         (unsigned long)ddev_control(good, 0x9999, NULL));

  drv_model_init();  /* leave the table as boot left it */
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

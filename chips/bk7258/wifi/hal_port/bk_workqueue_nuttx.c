/* NuttX implementation of the Armino workqueue ABI. */
#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/wqueue.h>

#include "bk_workqueue.h"

/* Critical-section policy is owned by bk7258_wifi_osal.c.
 *
 * Calling NuttX's enter_critical_section()/leave_critical_section() directly
 * from here left undefined references in bk_workqueue_nuttx.c.o (verified with
 * nm), while the identical call in bk7258_wifi_osal.c resolves. The primitive is
 * only declared by nuttx/spinlock.h, which this file reaches transitively
 * through nuttx/arch.h and the OSAL file does not, so the two translation units
 * did not agree on which form of the primitive they were using. Depending on
 * include order for that is the actual defect, so route it through the OSAL
 * wrappers, which also keeps a future SMP-correct policy in one place.
 *
 * Declared locally instead of including bk7258_wifi_internal.h: that header
 * drags in netdev_lowerhalf.h and iob.h, and NuttX network headers are a known
 * source of collisions with the vendored glue headers (see hal_port/include/
 * spinlock.h and components/log.h). The authoritative declarations are in
 * bk7258_wifi_internal.h.
 */

uint32_t bk7258_wifi_osal_enter_critical(void);
void     bk7258_wifi_osal_exit_critical(uint32_t flags);

struct bk_work_native
{
  struct work_s work;
  struct bk_work *public_work;
  struct bk_workqueue *queue;
};

static struct bk_workqueue *g_wq_default;
#define BK_NATIVE_WORK_SLOTS 32
static struct bk_work_native *g_native_works[BK_NATIVE_WORK_SLOTS];

static struct bk_work_native *bk_native_find(struct bk_work *work)
{
  unsigned int i;

  for (i = 0; i < BK_NATIVE_WORK_SLOTS; i++)
    {
      if (g_native_works[i] != NULL &&
          g_native_works[i]->public_work == work)
        {
          return g_native_works[i];
        }
    }

  return NULL;
}

static void bk_native_worker(void *arg)
{
  struct bk_work_native *native = arg;
  struct bk_work *work = native->public_work;
  struct bk_workqueue *queue = native->queue;
  irqstate_t flags = bk7258_wifi_osal_enter_critical();

  queue->work_current = work;
  work->exist = false;
  bk7258_wifi_osal_exit_critical(flags);

  work->handle(work->arg);

  flags = bk7258_wifi_osal_enter_critical();
  if (queue->work_current == work)
    queue->work_current = NULL;
  bk7258_wifi_osal_exit_critical(flags);
}

struct bk_workqueue *bk_workqueue_create(const char *name, uint8_t priority,
                                         size_t stack_size)
{
  struct bk_workqueue *queue = kmm_zalloc(sizeof(*queue));
  if (queue == NULL)
    return NULL;

  queue->name = name;
  queue->worker = work_queue_create(name, priority, NULL, stack_size, 1);
  if (queue->worker == NULL)
    {
      kmm_free(queue);
      return NULL;
    }
  return queue;
}

bk_err_t bk_work_init(struct bk_work *work, bk_work_handle_t handle, void *arg)
{
  struct bk_work_native *native;
  irqstate_t flags;
  unsigned int i;

  if (work == NULL || handle == NULL)
    return BK_ERR_NULL_PARAM;

  native = kmm_zalloc(sizeof(*native));
  if (native == NULL)
    return BK_FAIL;
  flags = bk7258_wifi_osal_enter_critical();
  if (bk_native_find(work) != NULL)
    {
      bk7258_wifi_osal_exit_critical(flags);
      kmm_free(native);
      return BK_FAIL;
    }
  for (i = 0; i < BK_NATIVE_WORK_SLOTS; i++)
    {
      if (g_native_works[i] == NULL)
        {
          native->public_work = work;
          g_native_works[i] = native;
          break;
        }
    }
  if (i == BK_NATIVE_WORK_SLOTS)
    {
      bk7258_wifi_osal_exit_critical(flags);
      kmm_free(native);
      return BK_FAIL;
    }
  bk7258_wifi_osal_exit_critical(flags);

  work->handle = handle;
  work->arg = arg;
  work->wq = NULL;
  work->exist = false;
  INIT_LIST_HEAD(&work->work_node);
  return BK_OK;
}

bk_err_t bk_work_run(struct bk_workqueue *queue, struct bk_work *work)
{
  struct bk_work_native *native;
  irqstate_t flags;
  int ret;

  if (queue == NULL || work == NULL || work->handle == NULL)
    return BK_ERR_NULL_PARAM;

  flags = bk7258_wifi_osal_enter_critical();
  if (queue->work_current == work)
    {
      bk7258_wifi_osal_exit_critical(flags);
      return BK_WORKQUEUE_WORK_RUNNING;
    }
  if (work->exist)
    {
      bk7258_wifi_osal_exit_critical(flags);
      return BK_WORKQUEUE_WORK_EXIST;
    }

  native = bk_native_find(work);
  if (native == NULL)
    {
      bk7258_wifi_osal_exit_critical(flags);
      return BK_FAIL;
    }
  native->queue = queue;
  work->wq = queue;
  work->exist = true;
  bk7258_wifi_osal_exit_critical(flags);

  ret = work_queue_wq(queue->worker, &native->work, bk_native_worker,
                      native, 0);
  if (ret < 0)
    {
      flags = bk7258_wifi_osal_enter_critical();
      work->exist = false;
      bk7258_wifi_osal_exit_critical(flags);
      return BK_FAIL;
    }
  return BK_OK;
}

bk_err_t bk_work_sched(struct bk_work *work)
{
  return bk_work_run(g_wq_default, work);
}

bk_err_t bk_work_cancel(struct bk_work *work)
{
  struct bk_workqueue *queue;

  if (work == NULL)
    return BK_ERR_NULL_PARAM;
  queue = work->wq;
  if (queue == NULL)
    return BK_OK;
  if (queue->work_current == work)
    return BK_WORKQUEUE_WORK_RUNNING;
  if (work->exist)
    return BK_WORKQUEUE_WORK_EXIST;

  work->wq = NULL;
  return BK_OK;
}

void bk_workqueue_init(void)
{
  if (g_wq_default == NULL)
    g_wq_default = bk_workqueue_create("syswq", CONFIG_WIFI_KMSG_TASK_PRIO,
                                       1024);
}

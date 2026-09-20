/*
 * NuttX owner for the small Armino event API needed by the vendor MAC path.
 *
 * The upstream service dispatches on a dedicated task. Thread-context posts
 * are dispatched synchronously to preserve their ordering. Hardware IRQ posts
 * are copied into a bounded preallocated ring and delivered by LPWORK: Wi-Fi
 * subscribers (notably scan completion) may take mutexes, copy results and
 * release SDK-owned memory, none of which is valid in hard interrupt context.
 */

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <common/bk_err.h>
#include <components/event.h>

#define BK7258_EVENT_CB_MAX 16
#define BK7258_EVENT_ISR_QUEUE_MAX 8
#define BK7258_EVENT_ISR_DATA_MAX  64

struct bk7258_event_cb_s
{
  bool used;
  event_module_t module;
  int event_id;
  event_cb_t callback;
  void *arg;
};

struct bk7258_event_deferred_s
{
  event_module_t module;
  int event_id;
  size_t data_size;
  uint8_t data[BK7258_EVENT_ISR_DATA_MAX];
};

struct bk7258_event_isr_queue_s
{
  struct work_s work;
  spinlock_t lock;
  bool scheduled;
  uint8_t head;
  uint8_t tail;
  uint8_t count;
  struct bk7258_event_deferred_s entries[BK7258_EVENT_ISR_QUEUE_MAX];
};

static struct bk7258_event_cb_s g_event_callbacks[BK7258_EVENT_CB_MAX];
static mutex_t g_event_lock;
static bool g_event_initialized;
static struct bk7258_event_isr_queue_s g_event_isr_queue =
{
  .lock = SP_UNLOCKED,
};

static bk_err_t bk7258_event_dispatch(event_module_t module, int event_id,
                                      void *data);
static void bk7258_event_isr_worker(void *arg);

static bk_err_t bk7258_event_defer_from_isr(event_module_t module,
                                             int event_id, void *data,
                                             size_t data_size)
{
  struct bk7258_event_isr_queue_s *queue = &g_event_isr_queue;
  irqstate_t flags;
  bool schedule = false;
  int ret = BK_OK;

  /* Event payload storage belongs to the posting vendor path. An IRQ post
   * must supply a bounded payload to copy; queueing its pointer would make
   * the later LPWORK callback dereference reclaimed stack/driver memory. */

  if (data_size > BK7258_EVENT_ISR_DATA_MAX ||
      (data_size != 0 && data == NULL))
    {
      return BK_ERR_NO_MEM;
    }

  flags = spin_lock_irqsave(&queue->lock);
  if (!g_event_initialized || queue->count == BK7258_EVENT_ISR_QUEUE_MAX)
    {
      ret = BK_ERR_NO_MEM;
    }
  else
    {
      struct bk7258_event_deferred_s *entry = &queue->entries[queue->tail];

      entry->module = module;
      entry->event_id = event_id;
      entry->data_size = data_size;
      if (data_size != 0)
        {
          memcpy(entry->data, data, data_size);
        }

      queue->tail = (queue->tail + 1) % BK7258_EVENT_ISR_QUEUE_MAX;
      queue->count++;
      if (!queue->scheduled)
        {
          queue->scheduled = true;
          schedule = true;
        }
    }
  spin_unlock_irqrestore(&queue->lock, flags);

  if (ret != BK_OK || !schedule)
    {
      return ret;
    }

  ret = work_queue(LPWORK, &queue->work, bk7258_event_isr_worker, queue, 0);
  if (ret < 0)
    {
      /* No worker can be running when its first queue request failed, so the
       * just-added tail entry can be removed while the ring is locked. */

      flags = spin_lock_irqsave(&queue->lock);
      queue->tail = (queue->tail + BK7258_EVENT_ISR_QUEUE_MAX - 1) %
                    BK7258_EVENT_ISR_QUEUE_MAX;
      queue->count--;
      queue->scheduled = false;
      spin_unlock_irqrestore(&queue->lock, flags);
      return BK_FAIL;
    }

  return BK_OK;
}

static void bk7258_event_isr_worker(void *arg)
{
  struct bk7258_event_isr_queue_s *queue = arg;

  for (;;)
    {
      struct bk7258_event_deferred_s entry;
      irqstate_t flags = spin_lock_irqsave(&queue->lock);

      if (!g_event_initialized || queue->count == 0)
        {
          queue->scheduled = false;
          spin_unlock_irqrestore(&queue->lock, flags);
          return;
        }

      entry = queue->entries[queue->head];
      queue->head = (queue->head + 1) % BK7258_EVENT_ISR_QUEUE_MAX;
      queue->count--;
      spin_unlock_irqrestore(&queue->lock, flags);

      (void)bk7258_event_dispatch(entry.module, entry.event_id,
                                  entry.data_size == 0 ? NULL : entry.data);
    }
}

bk_err_t bk_event_init(void)
{
  if (g_event_initialized)
    {
      return BK_OK;
    }

  memset(g_event_callbacks, 0, sizeof(g_event_callbacks));
  nxmutex_init(&g_event_lock);
  g_event_initialized = true;
  return BK_OK;
}

bk_err_t bk_event_deinit(void)
{
  irqstate_t flags;

  if (!g_event_initialized)
    {
      return BK_OK;
    }

  flags = spin_lock_irqsave(&g_event_isr_queue.lock);
  g_event_initialized = false;
  g_event_isr_queue.head = 0;
  g_event_isr_queue.tail = 0;
  g_event_isr_queue.count = 0;
  g_event_isr_queue.scheduled = false;
  spin_unlock_irqrestore(&g_event_isr_queue.lock, flags);

  (void)work_cancel_sync(LPWORK, &g_event_isr_queue.work);

  nxmutex_lock(&g_event_lock);
  memset(g_event_callbacks, 0, sizeof(g_event_callbacks));
  nxmutex_unlock(&g_event_lock);
  nxmutex_destroy(&g_event_lock);
  return BK_OK;
}

bk_err_t bk_event_register_cb(event_module_t module, int event_id,
                              event_cb_t callback, void *arg)
{
  int free_slot = -1;
  int i;

  if (!g_event_initialized)
    {
      return BK_ERR_EVENT_NOT_INIT;
    }

  if (module >= EVENT_MOD_COUNT || callback == NULL)
    {
      return BK_ERR_EVENT_MOD_OR_ID;
    }

  nxmutex_lock(&g_event_lock);
  for (i = 0; i < BK7258_EVENT_CB_MAX; i++)
    {
      if (g_event_callbacks[i].used)
        {
          if (g_event_callbacks[i].module == module &&
              g_event_callbacks[i].event_id == event_id &&
              g_event_callbacks[i].callback == callback)
            {
              nxmutex_unlock(&g_event_lock);
              return BK_ERR_EVENT_CB_EXIST;
            }
        }
      else if (free_slot < 0)
        {
          free_slot = i;
        }
    }

  if (free_slot >= 0)
    {
      g_event_callbacks[free_slot].module = module;
      g_event_callbacks[free_slot].event_id = event_id;
      g_event_callbacks[free_slot].callback = callback;
      g_event_callbacks[free_slot].arg = arg;
      g_event_callbacks[free_slot].used = true;
    }

  nxmutex_unlock(&g_event_lock);
  return free_slot >= 0 ? BK_OK : BK_ERR_NO_MEM;
}

bk_err_t bk_event_unregister_cb(event_module_t module, int event_id,
                                event_cb_t callback)
{
  bk_err_t result = BK_ERR_EVENT_NO_CB;
  int i;

  if (!g_event_initialized)
    {
      return BK_ERR_EVENT_NOT_INIT;
    }

  nxmutex_lock(&g_event_lock);
  for (i = 0; i < BK7258_EVENT_CB_MAX; i++)
    {
      if (g_event_callbacks[i].used && g_event_callbacks[i].module == module &&
          (event_id == EVENT_ID_ALL ||
           g_event_callbacks[i].event_id == event_id) &&
          (callback == NULL || g_event_callbacks[i].callback == callback))
        {
          g_event_callbacks[i].used = false;
          result = BK_OK;
        }
    }

  nxmutex_unlock(&g_event_lock);
  return result;
}

static bk_err_t bk7258_event_dispatch(event_module_t module, int event_id,
                                      void *data)
{
  struct bk7258_event_cb_s snapshot[BK7258_EVENT_CB_MAX];
  int count = 0;
  int i;

  nxmutex_lock(&g_event_lock);
  for (i = 0; i < BK7258_EVENT_CB_MAX; i++)
    {
      if (g_event_callbacks[i].used && g_event_callbacks[i].module == module &&
          (g_event_callbacks[i].event_id == EVENT_ID_ALL ||
           g_event_callbacks[i].event_id == event_id))
        {
          snapshot[count++] = g_event_callbacks[i];
        }
    }
  nxmutex_unlock(&g_event_lock);

  for (i = 0; i < count; i++)
    {
      snapshot[i].callback(snapshot[i].arg, module, event_id, data);
    }

  return BK_OK;
}

bk_err_t bk_event_post(event_module_t module, int event_id, void *data,
                       size_t data_size, uint32_t timeout)
{
  (void)timeout;

  if (!g_event_initialized)
    {
      return BK_ERR_EVENT_NOT_INIT;
    }

  if (module >= EVENT_MOD_COUNT)
    {
      return BK_ERR_EVENT_MOD;
    }

  if (up_interrupt_context())
    {
      return bk7258_event_defer_from_isr(module, event_id, data, data_size);
    }

  return bk7258_event_dispatch(module, event_id, data);
}

bk_err_t bk_event_dump(void)
{
  return g_event_initialized ? BK_OK : BK_ERR_EVENT_NOT_INIT;
}

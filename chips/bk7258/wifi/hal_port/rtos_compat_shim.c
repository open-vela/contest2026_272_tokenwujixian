/* Narrow Armino callback wrappers backed by the team OSAL. */
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>
#include <unistd.h>
#include <nuttx/kmalloc.h>

#include <pthread.h>

#include "bk7258_wifi_internal.h"
#include "os/os.h"

/* TEMPORARY BRING-UP DIAGNOSTIC -- see hal_port/include/bk7258_initseq.h for the
 * rationale and the removal recipe.  The markers below stand in for the Armino
 * FreeRTOS OSAL's "os:D(100):create <name>" lines, which this port never
 * produced because it replaced that layer with NuttX pthreads.  Their absence
 * from earlier captures was therefore not evidence about thread creation.
 *
 * Instrumenting the shim rather than the authority call sites keeps the
 * byte-identical vendored sources untouched while still bracketing every OSAL
 * call reached from rwnx_intf_init(): mutex, semaphore, queue and thread.
 */

#include <bk7258_initseq.h>

static size_t g_heap_sampled_minimum = SIZE_MAX;

bk_err_t rtos_init_queue(beken_queue_t *queue, const char *name,
                         uint32_t msg_size, uint32_t count)
{
  int ret;

  (void)name;

  BK7258_INITSEQ_NAMED("osal queue-init enter", name);
  ret = bk7258_wifi_osal_queue_create((uintptr_t *)queue, name, msg_size,
                                      count);
  BK7258_INITSEQ_NAMED_RET("osal queue-init leave", name, ret);

  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_deinit_queue(beken_queue_t *queue)
{
  return queue != NULL && *queue != NULL &&
         bk7258_wifi_osal_queue_delete((uintptr_t)*queue) == 0 ?
         (*queue = NULL, BK_OK) : BK_FAIL;
}

bk_err_t rtos_push_to_queue(beken_queue_t *queue, void *msg,
                            uint32_t timeout_ms)
{
  return bk7258_wifi_osal_queue_send((uintptr_t)*queue, msg, timeout_ms) < 0 ?
         BK_FAIL : BK_OK;
}

bk_err_t rtos_pop_from_queue(beken_queue_t *queue, void *msg,
                             uint32_t timeout_ms)
{
  return bk7258_wifi_osal_queue_recv((uintptr_t)*queue, msg, timeout_ms) < 0 ?
         BK_FAIL : BK_OK;
}

bool rtos_is_queue_empty(beken_queue_t *queue)
{
  return queue == NULL || *queue == NULL ||
         bk7258_wifi_osal_queue_empty((uintptr_t)*queue);
}

bool rtos_is_queue_full(beken_queue_t *queue)
{
  return queue != NULL && *queue != NULL &&
         bk7258_wifi_osal_queue_full((uintptr_t)*queue);
}

bk_err_t rtos_push_to_queue_front(beken_queue_t *queue, void *msg,
                                   uint32_t timeout_ms)
{
  return queue != NULL && *queue != NULL &&
         bk7258_wifi_osal_queue_send_front((uintptr_t)*queue, msg,
                                            timeout_ms) == 0 ? BK_OK : BK_FAIL;
}

bk_err_t rtos_init_semaphore(beken_semaphore_t *sem, int max_count)
{
  int ret;

  /* TEMPORARY DIAGNOSTIC (bk7258_initseq.h).  Semaphores carry no name, so the
   * marker reports max_count instead; the only semaphore created on the
   * bk_wifi_init() path is app_sema with max_count 1 (rw_task.c:1219).
   */

  BK7258_INITSEQ_RET("osal sem-init enter max_count", max_count);
  ret = bk7258_wifi_osal_sem_create((uintptr_t *)sem, max_count);
  BK7258_INITSEQ_RET("osal sem-init leave", ret);

  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_deinit_semaphore(beken_semaphore_t *sem)
{
  if (sem != NULL)
    {
      if (*sem != NULL)
        {
          (void)bk7258_wifi_osal_sem_delete((uintptr_t)*sem);
        }

      *sem = NULL;
    }

  return BK_OK;
}

bk_err_t rtos_get_semaphore(beken_semaphore_t *sem, uint32_t timeout_ms)
{
  return bk7258_wifi_osal_sem_wait((uintptr_t)*sem, timeout_ms) < 0 ?
         BK_FAIL : BK_OK;
}

bk_err_t rtos_set_semaphore(beken_semaphore_t *sem)
{
  return bk7258_wifi_osal_sem_post((uintptr_t)*sem) < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_init_mutex(beken_mutex_t *mutex)
{
  int ret;

  /* TEMPORARY DIAGNOSTIC (bk7258_initseq.h).  Mutexes carry no name; the only
   * one created on the bk_wifi_init() path is sr_mutex (rw_task.c:1218), the
   * first blocking candidate after cfg_param_init().
   */

  BK7258_INITSEQ("osal mutex-init enter");
  ret = bk7258_wifi_osal_mutex_create((uintptr_t *)mutex);
  BK7258_INITSEQ_RET("osal mutex-init leave", ret);

  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_deinit_mutex(beken_mutex_t *mutex)
{
  return mutex != NULL && *mutex != NULL &&
         bk7258_wifi_osal_mutex_delete((uintptr_t)*mutex) == 0 ?
         (*mutex = NULL, BK_OK) : BK_FAIL;
}

bk_err_t rtos_lock_mutex(beken_mutex_t *mutex)
{
  return mutex != NULL && *mutex != NULL &&
         bk7258_wifi_osal_mutex_lock((uintptr_t)*mutex) == 0 ?
         BK_OK : BK_FAIL;
}

bk_err_t rtos_unlock_mutex(beken_mutex_t *mutex)
{
  return mutex != NULL && *mutex != NULL &&
         bk7258_wifi_osal_mutex_unlock((uintptr_t)*mutex) == 0 ?
         BK_OK : BK_FAIL;
}

bk_err_t rtos_create_sram_thread(beken_thread_t *thread, uint8_t priority,
                                 const char *name,
                                 beken_thread_function_t function,
                                 uint32_t stack_size, beken_thread_arg_t arg)
{
  int ret;

  /* TEMPORARY DIAGNOSTIC (bk7258_initseq.h).  This is the stand-in for the
   * Armino OSAL's "os:D(100):create <name>" lines.  Two threads are created on
   * the bk_wifi_init() path: 'kmsgbk' (rw_task.c:1222) and 'core_thread'
   * (rw_task.c:1177, from core_thread_init()).  A third, the wpa_supplicant
   * thread, is created later from wpas_thread_start().
   *
   * The "enter" marker is what distinguishes "creation blocked" from "creation
   * never attempted": a hang inside the created thread body cannot stop
   * bk_wifi_init() from returning, but a hang in pthread_create() can.
   */

  BK7258_INITSEQ_NAMED("osal thread-create enter", name);
  ret = bk7258_wifi_osal_thread_create((uintptr_t *)thread, priority, name,
                                       function, arg, stack_size);
  BK7258_INITSEQ_NAMED_RET("osal thread-create leave", name, ret);

  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_create_thread(beken_thread_t *thread, uint8_t priority,
                            const char *name, void *function,
                            uint32_t stack_size, void *arg)
{
  return rtos_create_sram_thread(thread, priority, name,
                                 (beken_thread_function_t)function,
                                 stack_size, arg);
}

bk_err_t rtos_thread_set_priority(beken_thread_t *thread, int priority)
{
  return thread != NULL && *thread != NULL &&
         bk7258_wifi_osal_thread_set_priority((uintptr_t)*thread, priority) == 0 ?
         BK_OK : BK_FAIL;
}

bk_err_t rtos_delete_thread(beken_thread_t *thread)
{
  if (thread == NULL)
    {
      /* Beken maps NULL to vTaskDelete(NULL): terminate the calling task.
       * Every current caller is a detached pthread created by this OSAL, so
       * pthread_exit() is the matching NuttX ownership and cleanup path. */

      pthread_exit(NULL);
    }

  if (*thread != NULL)
    {
      if (bk7258_wifi_osal_thread_delete((uintptr_t)*thread) != 0)
        {
          return BK_FAIL;
        }

      *thread = NULL;
    }

  return BK_OK;
}

beken_thread_t *rtos_get_current_thread(void)
{
  static beken_thread_t current;
  current = (beken_thread_t)(uintptr_t)pthread_self();
  return &current;
}

bool rtos_is_current_thread(beken_thread_t *thread)
{
  pthread_t candidate;

  if (thread == NULL || *thread == NULL)
    {
      return false;
    }

  candidate = (pthread_t)(uintptr_t)*thread;
  return pthread_equal(pthread_self(), candidate) != 0;
}

size_t rtos_get_free_heap_size(void)
{
  struct mallinfo mi = kmm_mallinfo();
  return (size_t)mi.fordblks;
}

size_t rtos_get_total_heap_size(void)
{
  struct mallinfo mi = kmm_mallinfo();

  /* The NuttX allocator arena is the closest available equivalent to
   * FreeRTOS's configured heap size. */
  return (size_t)mi.arena;
}

size_t rtos_get_minimum_free_heap_size(void)
{
  struct mallinfo mi = kmm_mallinfo();
  irqstate_t flags = enter_critical_section();

  /* NuttX exposes current free space, not FreeRTOS's allocation-hook-backed
   * lifetime low watermark. Keep the minimum observed by this diagnostic API;
   * callers must not treat it as a whole-system memory guarantee. */
  if ((size_t)mi.fordblks < g_heap_sampled_minimum)
    {
      g_heap_sampled_minimum = (size_t)mi.fordblks;
    }

  leave_critical_section(flags);
  return g_heap_sampled_minimum;
}

void *ke_malloc(size_t size)
{
  /* ZEROING IS DELIBERATE (2026-09-01) -- kmm_zalloc, not kmm_malloc.
   *
   * The closed library contains code that allocates and then assumes the
   * memory is already zero.  One instance was found the hard way on-board:
   * libwifi's me_strategy_mem_init() allocates sta_info_tab and
   * sta_mgmt_entry_init() then treats entry+0x210 as an empty co_list, which
   * only works if the block arrives zeroed (see the explicit memset in
   * third_party/.../rw_ieee80211.c:rw_ieee80211_init -- keep that; it covers a
   * buffer the library allocates through libc malloc, not through this slot).
   *
   * Why the authority gets away with it: FreeRTOS's heap is the static array
   * `ucHeap[configTOTAL_HEAP_SIZE]` (heap_4.c:148), which lives in .bss and is
   * therefore zeroed by startup, so early first-time allocations are zero by
   * accident of placement.  NuttX's heap has already served the kernel, netdev
   * registration and more before Wi-Fi init runs, so the same allocation comes
   * back dirty.
   *
   * NOT a claim that FreeRTOS guarantees zeroed memory -- after any free/reuse
   * it does not.  The point is that the library's init-time allocations
   * happened to land on fresh .bss there and do not here, so we close the whole
   * class rather than chasing instances we cannot see inside the archive.
   *
   * Cost is a memset on init-path allocations; the only caller in the compiled
   * vendor set is rwnx_rx.c:812 (CFG_MSDU_MAX_LEN RX payloads).  Behaviour is
   * otherwise unchanged: zeroed memory is valid input everywhere plain
   * uninitialised memory was. */

  return kmm_zalloc(size);
}

void ke_free(void *ptr)
{
  kmm_free(ptr);
}

uint32_t rtos_disable_int(void)
{
  return bk7258_wifi_osal_disable_irq();
}

void rtos_enable_int(uint32_t level)
{
  bk7258_wifi_osal_enable_irq(level);
}

uint32_t rtos_get_time(void)
{
  return (uint32_t)bk7258_wifi_osal_time_ms();
}

bk_err_t rtos_delay_milliseconds(uint32_t ms)
{
  bk7258_wifi_osal_delay_ms(ms);
  return BK_OK;
}

struct bk_timer_priv
{
  struct wdog_s wdog;
  struct work_s work;
  beken2_timer_t *owner;
  timer_handler_t simple_function;
  void *simple_arg;
  timer_2handler_t oneshot_function;
  void *left_arg;
  void *right_arg;
  uint32_t period_ms;
  bool periodic;
  volatile bool initialized;
  volatile bool running;
};

#define BK_TIMER_MAGIC 0x42544d52u

static void bk_timer_expired(wdparm_t arg);

static void bk_timer_work(void *arg)
{
  struct bk_timer_priv *priv = arg;
  timer_2handler_t function;
  void *left_arg;
  void *right_arg;
  irqstate_t flags;

  flags = enter_critical_section();
  if (!priv->initialized || !priv->running)
    {
      leave_critical_section(flags);
      return;
    }

  if (!priv->periodic)
    priv->running = false;
  function = priv->oneshot_function;
  left_arg = priv->left_arg;
  right_arg = priv->right_arg;
  leave_critical_section(flags);

  if (priv->periodic)
    {
      if (priv->simple_function != NULL)
        priv->simple_function(priv->simple_arg);
    }
  else if (function != NULL)
    {
      function(left_arg, right_arg);
    }

  if (priv->periodic && priv->running)
    {
      wd_start(&priv->wdog, MSEC2TICK(priv->period_ms), bk_timer_expired,
               (wdparm_t)(uintptr_t)priv);
    }
}

static void bk_timer_expired(wdparm_t arg)
{
  struct bk_timer_priv *priv = (struct bk_timer_priv *)(uintptr_t)arg;
  irqstate_t flags;

  flags = enter_critical_section();
  if (priv->initialized && priv->running)
    {
      /* Keep the callback out of interrupt context and serialize it with
       * stop/deinit through the work queue cancellation primitives. */
      work_queue(HPWORK, &priv->work, bk_timer_work, priv, 0);
    }
  leave_critical_section(flags);
}

static struct bk_timer_priv *bk_timer_get(beken2_timer_t *timer)
{
  if (timer == NULL || timer->beken_magic != BK_TIMER_MAGIC ||
      timer->handle == NULL)
    {
      return NULL;
    }
  return timer->handle;
}

bk_err_t rtos_init_timer(beken_timer_t *timer, uint32_t ms,
                         timer_handler_t function, void *arg)
{
  struct bk_timer_priv *priv;

  if (timer == NULL || function == NULL || timer->handle != NULL)
    return BK_ERR_NULL_PARAM;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    return BK_ERR_NO_MEM;

  wd_init(&priv->wdog);
  priv->owner = NULL;
  priv->simple_function = function;
  priv->simple_arg = arg;
  priv->period_ms = ms;
  priv->periodic = true;
  priv->initialized = true;
  timer->function = function;
  timer->arg = arg;
  timer->handle = priv;
  return BK_OK;
}

bk_err_t rtos_start_timer(beken_timer_t *timer)
{
  struct bk_timer_priv *priv = (struct bk_timer_priv *)
    (timer == NULL ? NULL : timer->handle);
  if (priv == NULL)
    return BK_ERR_NOT_INIT;
  priv->running = true;
  return wd_start(&priv->wdog, MSEC2TICK(priv->period_ms), bk_timer_expired,
                  (wdparm_t)(uintptr_t)priv) < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_stop_timer(beken_timer_t *timer)
{
  struct bk_timer_priv *priv = (struct bk_timer_priv *)
    (timer == NULL ? NULL : timer->handle);
  if (priv == NULL)
    return BK_ERR_NOT_INIT;
  priv->running = false;
  wd_cancel(&priv->wdog);
  work_cancel_sync(HPWORK, &priv->work);
  return BK_OK;
}

bk_err_t rtos_reload_timer(beken_timer_t *timer)
{
  bk_err_t ret = rtos_stop_timer(timer);
  return ret == BK_OK ? rtos_start_timer(timer) : ret;
}

bk_err_t rtos_deinit_timer(beken_timer_t *timer)
{
  struct bk_timer_priv *priv = (struct bk_timer_priv *)
    (timer == NULL ? NULL : timer->handle);
  if (priv == NULL)
    return BK_ERR_NOT_INIT;
  rtos_stop_timer(timer);
  timer->handle = NULL;
  timer->function = NULL;
  timer->arg = NULL;
  kmm_free(priv);
  return BK_OK;
}

bool rtos_is_timer_running(beken_timer_t *timer)
{
  struct bk_timer_priv *priv = (struct bk_timer_priv *)
    (timer == NULL ? NULL : timer->handle);
  return priv != NULL && priv->running;
}

bk_err_t rtos_init_oneshot_timer(beken2_timer_t *timer, uint32_t ms,
                                 timer_2handler_t function, void *left_arg,
                                 void *right_arg)
{
  struct bk_timer_priv *priv;

  if (timer == NULL || function == NULL)
    return BK_ERR_NULL_PARAM;

  if (timer->beken_magic == BK_TIMER_MAGIC)
    return BK_FAIL;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    return BK_FAIL;

  wd_init(&priv->wdog);
  priv->owner = timer;
  priv->oneshot_function = function;
  priv->left_arg = left_arg;
  priv->right_arg = right_arg;
  priv->period_ms = ms;
  priv->initialized = true;
  timer->function = function;
  timer->left_arg = left_arg;
  timer->right_arg = right_arg;
  timer->handle = priv;
  timer->beken_magic = BK_TIMER_MAGIC;
  return BK_OK;
}

bk_err_t rtos_start_oneshot_timer(beken2_timer_t *timer)
{
  struct bk_timer_priv *priv = bk_timer_get(timer);
  irqstate_t flags;
  int ret;

  if (priv == NULL)
    return BK_ERR_NULL_PARAM;

  flags = enter_critical_section();
  if (!priv->initialized)
    {
      leave_critical_section(flags);
      return BK_FAIL;
    }
  priv->running = true;
  ret = wd_start(&priv->wdog, MSEC2TICK(priv->period_ms),
                 bk_timer_expired, (wdparm_t)(uintptr_t)priv);
  if (ret < 0)
    priv->running = false;
  leave_critical_section(flags);
  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_stop_oneshot_timer(beken2_timer_t *timer)
{
  struct bk_timer_priv *priv = bk_timer_get(timer);
  irqstate_t flags;

  if (priv == NULL)
    return BK_ERR_NULL_PARAM;
  flags = enter_critical_section();
  priv->running = false;
  wd_cancel(&priv->wdog);
  leave_critical_section(flags);
  work_cancel_sync(HPWORK, &priv->work);
  return BK_OK;
}

bool rtos_is_oneshot_timer_running(beken2_timer_t *timer)
{
  struct bk_timer_priv *priv = bk_timer_get(timer);
  return priv != NULL && priv->running;
}

bool rtos_is_oneshot_timer_init(beken2_timer_t *timer)
{
  return bk_timer_get(timer) != NULL;
}

bk_err_t rtos_oneshot_reload_timer(beken2_timer_t *timer)
{
  bk_err_t ret = rtos_stop_oneshot_timer(timer);
  if (ret != BK_OK)
    return ret;
  return rtos_start_oneshot_timer(timer);
}

bk_err_t rtos_deinit_oneshot_timer(beken2_timer_t *timer)
{
  struct bk_timer_priv *priv = bk_timer_get(timer);
  irqstate_t flags;

  if (priv == NULL)
    return BK_ERR_NULL_PARAM;
  flags = enter_critical_section();
  priv->initialized = false;
  priv->running = false;
  wd_cancel(&priv->wdog);
  leave_critical_section(flags);
  work_cancel_sync(HPWORK, &priv->work);
  timer->handle = NULL;
  timer->function = NULL;
  timer->left_arg = NULL;
  timer->right_arg = NULL;
  timer->beken_magic = 0;
  kmm_free(priv);
  return BK_OK;
}

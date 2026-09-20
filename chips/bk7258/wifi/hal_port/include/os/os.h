/*
 * Minimal Armino OS aggregate header for compile-only vendor glue.
 *
 * Runtime task, queue, and event implementations intentionally remain out of
 * this stage. Sources added while vendor runtime is disabled may include this
 * header only when they do not consume those APIs.
 */

#ifndef __BK7258_WIFI_GLUE_OS_OS_H
#define __BK7258_WIFI_GLUE_OS_OS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <common/bk_include.h>

#define RTOS_TAG                       "os"
#define RTOS_SUCCESS                   1
#define RTOS_FAILURE                   0
#define BEKEN_DEFAULT_WORKER_PRIORITY  6
#define BEKEN_APPLICATION_PRIORITY     7
#define BEKEN_NEVER_TIMEOUT            UINT32_MAX
#define BEKEN_WAIT_FOREVER             UINT32_MAX
#define BEKEN_NO_WAIT                  0
#define SECONDS                        1000u
#define CORE_QITEM_COUNT               64u

typedef void    *beken_thread_arg_t;
typedef uint8_t  beken_bool_t;
typedef uint32_t beken_time_t;
typedef uint32_t beken_event_flags_t;
typedef void    *beken_semaphore_t;
typedef void    *beken_mutex_t;
typedef void    *beken_thread_t;
typedef void    *beken_queue_t;
typedef void    *beken_event_t;
typedef void (*beken_thread_function_t)(beken_thread_arg_t arg);
typedef void (*timer_2handler_t)(void *left_arg, void *right_arg);
typedef void (*timer_handler_t)(void *arg);

typedef struct
{
  void *handle;
  timer_handler_t function;
  void *arg;
} beken_timer_t;

/* NuttX-backed one-shot timer handle. The callback executes from the NuttX
 * high-priority work queue; init/reload/stop semantics are implemented in the
 * rtos compatibility layer before RWNX TX queues are enabled. */
typedef struct
{
  void *handle;
  timer_2handler_t function;
  void *left_arg;
  void *right_arg;
  uint32_t beken_magic;
} beken2_timer_t;

uint32_t rtos_disable_int(void);
void rtos_enable_int(uint32_t level);
uint32_t rtos_get_time(void);
bk_err_t rtos_init_timer(beken_timer_t *timer, uint32_t ms,
                         timer_handler_t function, void *arg);
bk_err_t rtos_start_timer(beken_timer_t *timer);
bk_err_t rtos_stop_timer(beken_timer_t *timer);
bk_err_t rtos_reload_timer(beken_timer_t *timer);
bk_err_t rtos_deinit_timer(beken_timer_t *timer);
bool rtos_is_timer_running(beken_timer_t *timer);
bk_err_t rtos_delay_milliseconds(uint32_t ms);
bk_err_t rtos_init_oneshot_timer(beken2_timer_t *timer, uint32_t ms,
                                 timer_2handler_t function, void *left_arg,
                                 void *right_arg);
bk_err_t rtos_deinit_oneshot_timer(beken2_timer_t *timer);
bk_err_t rtos_start_oneshot_timer(beken2_timer_t *timer);
bk_err_t rtos_stop_oneshot_timer(beken2_timer_t *timer);
bool rtos_is_oneshot_timer_running(beken2_timer_t *timer);
bool rtos_is_oneshot_timer_init(beken2_timer_t *timer);
bk_err_t rtos_oneshot_reload_timer(beken2_timer_t *timer);

bk_err_t rtos_init_queue(beken_queue_t *queue, const char *name,
                         uint32_t msg_size, uint32_t count);
bk_err_t rtos_deinit_queue(beken_queue_t *queue);
bk_err_t rtos_push_to_queue(beken_queue_t *queue, void *msg,
                            uint32_t timeout_ms);
bk_err_t rtos_pop_from_queue(beken_queue_t *queue, void *msg,
                             uint32_t timeout_ms);
bool rtos_is_queue_empty(beken_queue_t *queue);
bool rtos_is_queue_full(beken_queue_t *queue);
bk_err_t rtos_push_to_queue_front(beken_queue_t *queue, void *msg,
                                   uint32_t timeout_ms);
bk_err_t rtos_init_semaphore(beken_semaphore_t *sem, int max_count);
bk_err_t rtos_deinit_semaphore(beken_semaphore_t *sem);
bk_err_t rtos_get_semaphore(beken_semaphore_t *sem, uint32_t timeout_ms);
bk_err_t rtos_set_semaphore(beken_semaphore_t *sem);
bk_err_t rtos_init_mutex(beken_mutex_t *mutex);
bk_err_t rtos_deinit_mutex(beken_mutex_t *mutex);
bk_err_t rtos_create_sram_thread(beken_thread_t *thread, uint8_t priority,
                                 const char *name,
                                 beken_thread_function_t function,
                                 uint32_t stack_size, beken_thread_arg_t arg);
bk_err_t rtos_create_thread(beken_thread_t *thread, uint8_t priority,
                            const char *name, void *function,
                            uint32_t stack_size, void *arg);
beken_thread_t *rtos_get_current_thread(void);
bool rtos_is_current_thread(beken_thread_t *thread);
bk_err_t rtos_delete_thread(beken_thread_t *thread);
size_t rtos_get_free_heap_size(void);
size_t rtos_get_total_heap_size(void);
size_t rtos_get_minimum_free_heap_size(void);

/* Real interrupt masking, aligned with the authoritative definition
 * (cp/include/os/os.h:43-47).  These were previously no-ops -- DISABLE just
 * zeroed the local and RESTORE discarded it -- so every vendor critical
 * section believed it had interrupts masked while it did not.  That covers
 * 42 call sites in the compiled vendor sources, mostly co_list_push_back /
 * co_list_extract on the shared rw_msg TX/RX lists (rw_msg_rx.c x13,
 * bk_workqueue.c x6, eloop.c x6, wifi_v2.c x5, rw_msdu.c x4, rw_task.c x3,
 * rw_msg_tx.c x2, rw_tx_buffering.c x2, hostapd_intf.c x1) -- i.e. exactly
 * the message paths the MM_RESET/MM_START confirm handshake runs on, shared
 * between the caller, the kmsgbk/core threads and the MAC ISR.
 *
 * rtos_disable_int/rtos_enable_int are declared above and already backed by
 * bk7258_wifi_osal_disable_irq/enable_irq (hal_port/rtos_compat_shim.c:207-215),
 * so this is the same primitive the authoritative macro uses. */
#define GLOBAL_INT_DECLARATION()  uint32_t irq_level
#define GLOBAL_INT_DISABLE()      do { irq_level = rtos_disable_int(); } while (0)
#define GLOBAL_INT_RESTORE()      do { rtos_enable_int(irq_level); } while (0)

/* Mailbox-backed PHY operation coordination is intentionally not enabled
 * until the vendor runtime and IPC adaptation are validated. */
#ifndef CONFIG_PHY_MB
#  define CONFIG_PHY_MB 0
#endif

#endif /* __BK7258_WIFI_GLUE_OS_OS_H */

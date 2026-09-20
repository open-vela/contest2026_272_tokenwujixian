/*
 * chips/bk7258/wifi/hal_port/include/os/rtos_ext.h
 *
 * NuttX reimplementation of the Armino rtos extension API (event_ex + hisr).
 * hisr (High-priority ISR) maps onto the NuttX HP work queue.
 */

#ifndef __BK7258_WIFI_GLUE_OS_RTOS_EXT_H
#define __BK7258_WIFI_GLUE_OS_RTOS_EXT_H

#include <stdint.h>

#include "os/os.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  beken_semaphore_t event_semaphore;
  uint32_t event_flag;
} rtos_event_ext_t;

bk_err_t rtos_init_event_ex(rtos_event_ext_t *event_ext);
bk_err_t deinit_event_ex(rtos_event_ext_t *event_ext);
uint32_t set_event_ex(rtos_event_ext_t *event_ext, uint32_t event);
uint32_t wait_event_ex(rtos_event_ext_t *event_ext, uint32_t event,
                       uint32_t timeout_ms);

typedef void (*rtos_hisr_cb_t)(void);

void rtos_activate_hisr(rtos_hisr_cb_t isr, int8_t priority);
bk_err_t rtos_create_hisr(rtos_hisr_cb_t isr, int8_t priority, char *name);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_OS_RTOS_EXT_H */

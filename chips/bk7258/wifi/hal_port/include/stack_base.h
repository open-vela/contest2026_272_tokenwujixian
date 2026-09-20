/*
 * chips/bk7258/wifi/hal_port/include/stack_base.h
 *
 * Crash-dump hook registration. bk_wifi_adapter.c:1147 really calls
 * rtos_regist_wifi_dump_hook() (inside its register_wifi_dump_hook wrapper), so
 * this is a live reference, not a spare include.
 *
 * Only that one function is declared. Upstream's header also has
 * arch_dump_exception_stack / stack_mem_dump / rtos_regist_ble_dump_hook /
 * rtos_regist_plat_dump_hook; none are referenced by the compiled vendored set,
 * and NuttX has its own crash dump path.
 *
 * Implementation in hal_port/misc_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_STACK_BASE_H
#define __BK7258_WIFI_GLUE_STACK_BASE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hook_func)(void);

void rtos_regist_wifi_dump_hook(hook_func wifi_func);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_STACK_BASE_H */

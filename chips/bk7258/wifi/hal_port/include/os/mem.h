/*
 * chips/bk7258/wifi/hal_port/include/os/mem.h
 *
 * NuttX reimplementation of the Armino os_mem* wrappers. libc aliases plus
 * word-access macros; os_malloc/os_free/os_zalloc/os_realloc resolve to the
 * kernel-heap-backed os_malloc_debug/os_free_debug implemented in os_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_OS_MEM_H
#define __BK7258_WIFI_GLUE_OS_MEM_H

#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define os_write_word(addr, val) *((volatile uint32_t *)(addr)) = (val)
#define os_read_word(addr, val)  (val) = *((volatile uint32_t *)(addr))
#define os_get_word(addr)        *((volatile uint32_t *)(addr))

typedef enum {
  HEAP_MEM_TYPE_DEFAULT,
  HEAP_MEM_TYPE_SRAM,
  HEAP_MEM_TYPE_PSRAM,
  MEM_TYPE_MAX = 0xf
} beken_mem_type_t;

#define os_memcpy(o, i, n)   memcpy(o, i, n)
#define os_memset(b, c, n)   memset(b, c, n)
#define os_memcmp(s1, s2, n) memcmp(s1, s2, n)
#define os_memcmp_const(a, b, n) memcmp(a, b, n)
#define os_realloc(p, s)     realloc(p, s)

void *os_malloc_debug(const char *func_name, int line, size_t size,
                      int need_zero);
void  os_free_debug(const char *func_name, int line, void *pv);
void *os_malloc_wifi_buffer(size_t size);
void *os_memmove(void *dest, const void *src, size_t n);

#define os_malloc(size)  os_malloc_debug(__FUNCTION__, __LINE__, size, 0)
#define os_free(p)       os_free_debug(__FUNCTION__, __LINE__, p)
#define os_zalloc(size)  os_malloc_debug(__FUNCTION__, __LINE__, size, 1)

#define psram_free        os_free

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_OS_MEM_H */

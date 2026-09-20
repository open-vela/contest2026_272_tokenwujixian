/*
 * chips/bk7258/wifi/hal_port/include/common/bk_generic.h
 *
 * NuttX reimplementation of the Armino generic utility macros used by the
 * vendored glue (MAX/MIN/ARRAY_SIZE/round_up/...).
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_GENERIC_H
#define __BK7258_WIFI_GLUE_COMMON_BK_GENERIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FUNCPTR)(void);
typedef void (*FUNC_1PARAM_PTR)(void *ctxt);
typedef void (*FUNC_2PARAM_PTR)(void *arg, uint8_t vif_idx);

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define min_t(type, x, y) ({ \
  type __min1 = (x); \
  type __min2 = (y); \
  __min1 < __min2 ? __min1 : __min2; })

#define max_t(type, x, y) ({ \
  type __max1 = (x); \
  type __max2 = (y); \
  __max1 > __max2 ? __max1 : __max2; })

#define swap(a, b) \
  do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef BIT
#define BIT(i) (UINT32_C(1) << (i))
#endif

/* bk_generic.h:148 upstream.  Needed by the imported authority GPIO HAL, whose
 * pin sets are 64-bit (gpio_hal.c:257 uses BIT64 to test a gpios mask across
 * all SOC_GPIO_NUM=56 pins). */
#ifndef BIT64
#define BIT64(i) (1LL << (i))
#endif

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))
#define FIELD_SIZEOF(t, f) (sizeof(((t *)0)->f))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, divisor) (((x) + (divisor) / 2) / (divisor))

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_GENERIC_H */

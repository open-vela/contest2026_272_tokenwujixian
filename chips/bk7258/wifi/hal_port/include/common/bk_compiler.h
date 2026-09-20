/*
 * chips/bk7258/wifi/hal_port/include/common/bk_compiler.h
 *
 * NuttX reimplementation of the Armino compiler-attribute macros used by the
 * vendored glue. Interface-compatible, independently written.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_COMPILER_H
#define __BK7258_WIFI_GLUE_COMMON_BK_COMPILER_H

#define __BK_INLINE          static inline
#define __BK_STATIC          static
#define __BK_SECTION(x)      __attribute__((section(x)))
#define __bk_deprecated      __attribute__((deprecated))
#define __bk_weak            __attribute__((weak))
#define __bk_must_check      __attribute__((warn_unused_result))
#define __bk_packed          __attribute__((packed))

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_COMPILER_H */

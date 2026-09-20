/*
 * chips/bk7258/wifi/hal_port/include/lwip/arch.h
 *
 * lwIP fixed-width type aliases used by the vendored glue. Written for NuttX,
 * not vendored: only the `struct pbuf` packet layout is reused from lwIP, the
 * protocol stack is replaced by nuttx/net (plan §11.4.6). Upstream's arch.h is
 * 397 lines and pulls in <unistd.h> and arch/cc.h; the glue only needs these
 * typedefs.
 *
 * Self-contained by design -- no NuttX kernel headers in any header that
 * reaches the vendored translation units (see components/log.h).
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_ARCH_H
#define __BK7258_WIFI_GLUE_LWIP_ARCH_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

#ifndef PACK_STRUCT_STRUCT
#  define PACK_STRUCT_BEGIN
#  define PACK_STRUCT_END
#  define PACK_STRUCT_STRUCT __attribute__((packed))
#  define PACK_STRUCT_FIELD(x) x
#endif

#endif /* __BK7258_WIFI_GLUE_LWIP_ARCH_H */

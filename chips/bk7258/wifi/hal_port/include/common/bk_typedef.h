/*
 * chips/bk7258/wifi/hal_port/include/common/bk_typedef.h
 *
 * NuttX reimplementation of the Armino base integer/bool typedefs used by the
 * vendored glue. Interface-compatible, independently written. size_t is NOT
 * redefined here (stddef.h/stdint.h already provide it), fixing the upstream
 * duplicate/conflicting `typedef unsigned int size_t`.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_TYPEDEF_H
#define __BK7258_WIFI_GLUE_COMMON_BK_TYPEDEF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char          uint8;
typedef signed   char          int8;
typedef unsigned short         uint16;
typedef signed   short         int16;
typedef unsigned int           uint32;
typedef signed   int           int32;
typedef unsigned long long     uint64;
typedef signed   long long     int64;

typedef unsigned char          UINT8;
typedef signed   char          INT8;
typedef unsigned short         UINT16;
typedef signed   short         INT16;
typedef unsigned int           UINT32;
typedef signed   int           INT32;
typedef unsigned long long     UINT64;
typedef signed   long long     INT64;
typedef float                  FP32;
typedef double                 FP64;

typedef unsigned char          BOOLEAN;
#ifndef BOOL
typedef unsigned char          BOOL;
#endif

#define BK_TRUE                1
#define BK_FALSE               0

#define LPVOID                 void *
#define VOID                   void

typedef volatile signed long       VS32;
typedef volatile signed short      VS16;
typedef volatile signed char       VS8;
typedef volatile signed long const VSC32;
typedef volatile signed short const VSC16;
typedef volatile signed char const VSC8;

typedef volatile unsigned long       VU32;
typedef volatile unsigned short      VU16;
typedef volatile unsigned char       VU8;
typedef volatile unsigned long const VUC32;
typedef volatile unsigned short const VUC16;
typedef volatile unsigned char const VUC8;

#ifndef HAVE_UTYPES
typedef unsigned char      u8;
typedef signed char        s8;
typedef unsigned short     u16;
typedef signed short       s16;
typedef unsigned int       u32;
typedef signed int         s32;
#endif

typedef unsigned long long u64;
typedef long long          s64;

typedef unsigned int       __u32;
typedef int                __s32;
typedef unsigned short     __u16;
typedef signed short       __s16;
typedef unsigned char      __u8;
typedef unsigned long long __u64;
typedef uint16_t           __le16;
typedef uint32_t           __le32;

/* Identity conversions: BK7258 is little-endian, so these are no-ops.  Same
 * shape as the authority's definitions (wpa_supplicant
 * src/common/ieee802_11_defs.h:2513-2514), which also define both as identity.
 *
 * cpu_to_le32 added 2026-09-01: only cpu_to_le16 was defined here, so enabling
 * CONFIG_WIFI4 (which the authority has at 1) failed to link -- the VHT branch
 * of rw_msg_send_me_config_req (rw_msg_tx.c:311) is the only user. */
#ifndef cpu_to_le16
#  define cpu_to_le16(v) ((uint16_t)(v))
#endif
#ifndef cpu_to_le32
#  define cpu_to_le32(v) ((uint32_t)(v))
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_TYPEDEF_H */

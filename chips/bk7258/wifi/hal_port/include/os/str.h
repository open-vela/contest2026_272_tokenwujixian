/*
 * chips/bk7258/wifi/hal_port/include/os/str.h
 *
 * NuttX reimplementation of the Armino os_str* wrappers. They are libc
 * aliases; NuttX libc provides the backing functions. strtok_r is exposed
 * directly (glue uses it).
 */

#ifndef __BK7258_WIFI_GLUE_OS_STR_H
#define __BK7258_WIFI_GLUE_OS_STR_H

#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define os_strlen(s)                 strlen(s)
#define os_strcmp(s1, s2)            strcmp(s1, s2)
#define os_strncmp(s1, s2, n)        strncmp(s1, s2, n)
#define os_snprintf(buf, size, ...)  snprintf(buf, size, __VA_ARGS__)
#define os_vsnprintf(b, s, f, a)     vsnprintf(b, s, f, a)
#define os_strncpy(o, i, n)          strncpy(o, i, n)
#define os_strtoul(n, e, b)          strtoul(n, e, b)
#define os_strcpy(o, i)              strcpy(o, i)
#define os_strchr(s, c)              strchr(s, c)
#define os_strdup(s)                 strdup(s)
#define os_strcasecmp(s1, s2)        strcasecmp(s1, s2)
#define os_strncasecmp(s1, s2, n)    strncasecmp(s1, s2, n)
#define os_strrchr(s, c)             strrchr(s, c)
#define os_strstr(h, n)              strstr(h, n)
#define os_strlcpy(d, s, z)          strlcpy(d, s, z)

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_OS_STR_H */

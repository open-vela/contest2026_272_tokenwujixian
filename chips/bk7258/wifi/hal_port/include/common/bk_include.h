/*
 * chips/bk7258/wifi/hal_port/include/common/bk_include.h
 *
 * NuttX reimplementation of the Armino aggregate include header. Includes the
 * base config/typedef/utility headers; the Armino soc/soc.h register header is
 * intentionally NOT included (registers come from the team chip layer via a
 * separate mapping once needed).
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_INCLUDE_H
#define __BK7258_WIFI_GLUE_COMMON_BK_INCLUDE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <common/sys_config.h>
#include <common/bk_typedef.h>
#include <common/bk_generic.h>
#include <common/bk_err.h>
#include <common/bk_kernel_err.h>

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_INCLUDE_H */

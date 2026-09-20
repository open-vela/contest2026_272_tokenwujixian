/*
 * chips/bk7258/wifi/hal_port/include/common/bk_assert.h
 *
 * NuttX reimplementation of the Armino BK_ASSERT used by the vendored glue.
 * Maps onto NuttX DEBUGASSERT; no custom dump/reboot hook at skeleton stage.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_ASSERT_H
#define __BK7258_WIFI_GLUE_COMMON_BK_ASSERT_H

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BK_ASSERT_TAG "ASSERT"

#define BK_ASSERT(exp)          DEBUGASSERT(exp)
#define BK_ASSERT_EX(exp, ...)  DEBUGASSERT(exp)
#define BK_ASSERT_DUMP(f, l)

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_ASSERT_H */

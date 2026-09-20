/*
 * chips/bk7258/wifi/hal_port/include/driver/ckmn.h
 *
 * NuttX reimplementation of the Armino CKMN (clock manager) driver callbacks
 * used by the vendored glue. Only the RC32k PPM getter the glue calls is
 * declared; it returns 0 (uncalibrated) until the team ckmn driver exists.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_CKMN_H
#define __BK7258_WIFI_GLUE_DRIVER_CKMN_H

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_ckmn_driver_init(void);
bk_err_t bk_ckmn_driver_deinit(void);
bk_err_t bk_ckmn_driver_get_rc32k_ppm(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_CKMN_H */

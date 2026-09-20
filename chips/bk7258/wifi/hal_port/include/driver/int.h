/* NuttX compatibility declarations for the Armino ICU surface. */
#ifndef __BK7258_WIFI_GLUE_DRIVER_INT_H
#define __BK7258_WIFI_GLUE_DRIVER_INT_H

#include <stdint.h>
#include <common/bk_err.h>
#include <driver/int_types.h>

bk_err_t bk_icu_driver_init(void);
bk_err_t bk_icu_driver_deinit(void);
bk_err_t bk_int_isr_register(icu_int_src_t dev, int_group_isr_t isr, void *arg);
bk_err_t bk_int_isr_unregister(icu_int_src_t src);
bk_err_t bk_int_set_priority(icu_int_src_t src, uint32_t int_priority);
bk_err_t bk_int_set_group(void);
bk_err_t bk_int_register_mac_ps_callback(int_mac_ps_callback_t mac_ps_cb);

#endif

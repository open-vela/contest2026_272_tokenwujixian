/*
 * chips/bk7258/wifi/hal_port/include/bk_mcu_ps.h
 *
 * Empty compatibility header. bk_wifi_adapter.c includes it and defines
 * mcu_ps_machw_cal/reset/init and mcu_ps_bcn_callback wrappers that get
 * registered into g_wifi_funcs -- but every wrapper body has its actual call
 * commented out upstream (bk_wifi_adapter.c:637, 649, 655), so no symbol from
 * this header is referenced.
 *
 * MCU power save is out of STA MVP scope (CONFIG_MCU_PS is 0 in sys_config.h).
 */

#ifndef __BK7258_WIFI_GLUE_BK_MCU_PS_H
#define __BK7258_WIFI_GLUE_BK_MCU_PS_H

#endif /* __BK7258_WIFI_GLUE_BK_MCU_PS_H */

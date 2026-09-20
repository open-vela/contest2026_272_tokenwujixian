/*
 * chips/bk7258/wifi/hal_port/include/bk_misc.h
 *
 * Busy-delay helpers. bk_wifi_adapter.c:223 wraps delay() into g_wifi_funcs,
 * so libwifi.a calls it.
 *
 * Signatures are upstream's. Note that delay() is NOT a time unit: upstream
 * implements it as a bare nested busy loop (bk_system/delay.c:13, `for i<num:
 * for j<100: ;`), so its duration depends on core clock and optimisation
 * level. Mapping it to usleep()/up_udelay() would silently change the timing
 * the vendor runtime was tuned against, so hal_port/misc_shim.c reproduces the
 * loop instead.
 */

#ifndef __BK7258_WIFI_GLUE_BK_MISC_H
#define __BK7258_WIFI_GLUE_BK_MISC_H

#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

void delay(INT32 num);
void delay_ms(UINT32 ms_count);
void delay_sec(UINT32 sec_count);
void delay_tick(UINT32 tick_count);
void bk_delay_us(UINT32 us);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BK_MISC_H */

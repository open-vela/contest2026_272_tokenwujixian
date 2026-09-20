/*
 * chips/bk7258/wifi/hal_port/include/bk_fake_clock.h
 *
 * Empty compatibility header. rwnx_txq.c includes it and calls bk_get_tick(),
 * but upstream's bk_fake_clock.h does not declare that function -- it only has
 * the fclk_* helpers, none of which the compiled glue uses. bk_get_tick() is
 * declared in components/system.h (and implemented in hal_port/system_shim.c over
 * clock_systime_ticks()).
 *
 * Add fclk_* declarations here only if a compiled vendored file starts needing
 * them.
 */

#ifndef __BK7258_WIFI_GLUE_BK_FAKE_CLOCK_H
#define __BK7258_WIFI_GLUE_BK_FAKE_CLOCK_H

#endif /* __BK7258_WIFI_GLUE_BK_FAKE_CLOCK_H */

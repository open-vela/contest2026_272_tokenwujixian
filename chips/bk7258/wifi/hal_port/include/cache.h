/*
 * chips/bk7258/wifi/hal_port/include/cache.h
 *
 * Empty compatibility header. bk_wifi_adapter.c includes it for
 * flush_all_dcache(), but its only call site sits inside
 * `#if CONFIG_CACHE_ENABLE` (bk_wifi_adapter.c:750), and that config is
 * deliberately left undefined -- cache attributes for the shared MAC buffers
 * are not established yet (see the deviation list in common/sys_config.h).
 * So the wrapper compiles to an empty function and no symbol is referenced.
 *
 * When cache is enabled, this must declare flush_dcache(void *va, long size)
 * and flush_all_dcache(void), bridged to NuttX up_clean_dcache/
 * up_invalidate_dcache_all. Getting that wrong is silent data corruption on the
 * MAC data path, so treat it as part of the cache work, not a header fix.
 */

#ifndef __BK7258_WIFI_GLUE_CACHE_H
#define __BK7258_WIFI_GLUE_CACHE_H

#endif /* __BK7258_WIFI_GLUE_CACHE_H */

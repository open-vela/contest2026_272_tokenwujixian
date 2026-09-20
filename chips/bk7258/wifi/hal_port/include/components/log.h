/*
 * chips/bk7258/wifi/hal_port/include/components/log.h
 *
 * NuttX reimplementation of the Armino BK_LOG* macros used by the vendored
 * glue. Maps onto NuttX syslog.  The default authority-compatible configuration
 * compiles verbose logging out; this diagnostic worktree intentionally enables
 * it to trace the ADC/PHY initialization path.
 */

#ifndef __BK7258_WIFI_GLUE_COMPONENTS_LOG_H
#define __BK7258_WIFI_GLUE_COMPONENTS_LOG_H

#include <syslog.h>

/* Verbose is normally compiled out, and this is load-bearing rather than cosmetic.
 *
 * Two vendored call sites print the passphrase in the clear:
 *   wifi_v2.c:2781  WIFI_LOGV("sta config, ssid=%s password=%s security=%d")
 *                   -- reached from bk_wifi_sta_set_config() via :3002
 *   wifi_v2.c:2828  the same line in wifi_sta_get_global_config()
 *                   -- reached from bk_wifi_sta_start() via :2347
 * Both sit directly on the STA association path, so they went live the moment
 * that path was wired up; before that they were unreachable code.
 *
 * The authority never prints them.  Its log.h computes BK_LOG_LEVEL from
 * CONFIG_LOG_LEVEL (cp/include/components/log.h:40-56): the reference build
 * sets CONFIG_LOG_LEVEL 6 == BK_LOG_DEFAULT and does not define
 * CONFIG_DEBUG_VERSION, which yields BK_LOG_LEVEL == BK_LOG_INFO == 3.  Since
 * BK_LOGV requires >= BK_LOG_VERBOSE == 5 (:90-95), it expands to
 * `(void)(format, ...)` there.  Our unconditional version therefore was not
 * just a credential leak, it was a behavioural divergence: output the authority
 * does not produce.
 *
 * The normal policy belongs here and not in wifi_v2.c.  That file's function bodies are
 * byte-identical to the authority copy (the only differences are include
 * depth, one #if/#ifdef, and the g_wifi_funcs/g_wifi_vars definition that
 * lives in funcs_fill.c here), and that identity is the evidence the whole
 * association design rests on.  Editing a log line inside it would spend that
 * evidence to fix a problem that is ours, in our own macro layer.
 *
 * This diagnostic override deliberately emits verbose records through the same
 * LOG_DEBUG sink as BK_LOGD.  It is not safe for distributable firmware: some
 * vendor WIFI_LOGV call sites print the configured STA password/PSK.  Keep
 * UART captures local or redact credentials before sharing, then restore the
 * authority-compatible no-op before a production build.
 *
 * BK_LOGD is deliberately NOT gated, even though the authority also compiles it
 * out at level 3.  The 4-way handshake fingerprint this bring-up is verified
 * against -- `State: ... -> 4WAY_HANDSHAKE` / `-> COMPLETED` -- reaches this
 * header through wpa_dbg() -> WPA_LOGD -> BK_LOGD (wpa_debug.c:71,
 * wpa_debug.h:27), so gating D would delete the evidence we need to read off
 * the serial port.  Keeping D is extra output relative to the authority, not
 * missing output, and it carries no credentials.
 */

#define BK_LOGE(tag, fmt, ...) syslog(LOG_ERR, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGW(tag, fmt, ...) syslog(LOG_WARNING, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGI(tag, fmt, ...) syslog(LOG_INFO, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGD(tag, fmt, ...) syslog(LOG_DEBUG, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGV(tag, fmt, ...) syslog(LOG_DEBUG, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOG_RAW(tag, fmt, ...) syslog(LOG_DEBUG, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_MAC_FORMAT "%02x:%02x:%02x:%02x:%02x:%02x"
#define BK_MAC_STR(a) ((a)[0]), ((a)[1]), ((a)[2]), ((a)[3]), ((a)[4]), ((a)[5])

#define BK_LOG_FLUSH()

#endif /* __BK7258_WIFI_GLUE_COMPONENTS_LOG_H */

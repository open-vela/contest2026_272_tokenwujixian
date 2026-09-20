/* Armino hostapd's no-process entry-point declarations are used by the
 * vendored TX/RX glue. Keep the public type surface local to the port. */
#ifndef BK7258_WIFI_WPA_COMPAT_MAIN_NONE_H
#define BK7258_WIFI_WPA_COMPAT_MAIN_NONE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct wpa_supplicant;
struct hapd_interfaces;
struct wpah_msg;

/* The STA provider uses a fixed interface name; SoftAP entry points are not
 * part of the runtime contract and are intentionally not declared here. */
#ifndef BK7258_WIFI_WPA_STA_IFNAME
#  define BK7258_WIFI_WPA_STA_IFNAME "wlan0"
#endif

void hostapd_thread_start(void);
void hostapd_thread_stop(void);
int supplicant_main_entry(char *oob_ssid);

#endif

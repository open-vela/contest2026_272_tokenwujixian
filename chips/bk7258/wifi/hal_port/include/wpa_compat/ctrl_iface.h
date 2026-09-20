/* Minimal public declaration surface used by rw_ieee80211.c. */
#ifndef BK7258_WIFI_WPA_COMPAT_CTRL_IFACE_H
#define BK7258_WIFI_WPA_COMPAT_CTRL_IFACE_H

#include "wpa_ctrl.h"

int wpa_ctrl_event_copy(int event, void *data, int len);

#endif

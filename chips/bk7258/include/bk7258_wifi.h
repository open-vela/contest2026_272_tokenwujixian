/****************************************************************************
 * chips/bk7258/include/bk7258_wifi.h
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H

#include <nuttx/config.h>

#include <stdbool.h>

#ifdef CONFIG_BK7258_WIFI
int bk7258_wifi_initialize(void);
bool bk7258_wifi_is_ready(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H */

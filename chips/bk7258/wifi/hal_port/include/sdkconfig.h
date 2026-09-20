/*
 * chips/bk7258/wifi/hal_port/include/sdkconfig.h
 *
 * Upstream Armino generates sdkconfig.h per build and the vendored glue
 * includes it by bare name (e.g. rwnx_config.h:33). Our values live in
 * common/sys_config.h, with provenance recorded per macro; this header only
 * forwards, so there is a single source of truth.
 */

#ifndef __BK7258_WIFI_GLUE_SDKCONFIG_H
#define __BK7258_WIFI_GLUE_SDKCONFIG_H

#include <common/sys_config.h>

#endif /* __BK7258_WIFI_GLUE_SDKCONFIG_H */

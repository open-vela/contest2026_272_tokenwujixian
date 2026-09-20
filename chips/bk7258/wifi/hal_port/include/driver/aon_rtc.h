/*
 * chips/bk7258/wifi/hal_port/include/driver/aon_rtc.h
 *
 * Keep this legacy glue include name as a compatibility entry point, but make
 * the complete Armino BK7258 authority header the one definition source.  The
 * previous local reimplementation declared a reduced surface (aon_rtc_id_t as a
 * plain uint32_t, no alarm API) to match init/deinit stubs that never started
 * the AON RTC counter; the authority driver under chips/bk7258/aon_rtc/ now
 * provides the real implementation.
 *
 * The nested <driver/aon_rtc_types.h> include resolves to the authority header
 * as well, because no glue-local file of that name exists.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_AON_RTC_H
#define __BK7258_WIFI_GLUE_DRIVER_AON_RTC_H

#include "../../../../aon_rtc/armino/include/driver/aon_rtc.h"

#endif /* __BK7258_WIFI_GLUE_DRIVER_AON_RTC_H */

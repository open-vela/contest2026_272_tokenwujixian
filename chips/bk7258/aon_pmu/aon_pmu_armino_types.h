/* Source-local NuttX ABI boundary for the otherwise unmodified authority
 * AON PMU HAL and driver modules.  Legacy glue headers now forward directly
 * to the authority type/API sources, so no second PMU enum/address map exists.
 * Behavior remains in the imported authority C and generated LL sources. */

#ifndef __BK7258_AON_PMU_AUTHORITY_TYPES_H
#define __BK7258_AON_PMU_AUTHORITY_TYPES_H

#include <common/bk_include.h>

#include <sys_types.h>
#include <aon_pmu_hal.h>

#ifndef PM_POWER_ON_ROSC_STABILITY_TIME
#  define PM_POWER_ON_ROSC_STABILITY_TIME (26000 * 2)
#endif

#ifndef __IRAM_SEC
#  define __IRAM_SEC
#endif

#ifndef AON_RTC_EXTERN_32K_CLOCK_FREQ
#  define AON_RTC_EXTERN_32K_CLOCK_FREQ 32768u
#endif

#ifndef AON_RTC_DEFAULT_CLOCK_FREQ
#  define AON_RTC_DEFAULT_CLOCK_FREQ 32000u
#endif

void bk_rtc_set_clock_freq(uint32_t frequency);

#endif /* __BK7258_AON_PMU_AUTHORITY_TYPES_H */

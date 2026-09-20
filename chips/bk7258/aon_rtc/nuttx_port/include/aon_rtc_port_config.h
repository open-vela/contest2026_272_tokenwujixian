/****************************************************************************
 * chips/bk7258/aon_rtc/nuttx_port/include/aon_rtc_port_config.h
 *
 * Configuration gate for the directly imported BK7258 authority AON RTC
 * module.  Values are taken from the authority BK7258 CP build so the imported
 * driver compiles the same code paths as the authority firmware:
 *
 *   projects/app/cp/config/bk7258/config
 *   cp/middleware/soc/bk7258/bk7258.defconfig
 *
 * Reached globally through common/sys_config.h -> common/bk_include.h, NOT as
 * a per-module force-include.  That is deliberate: CONFIG_AON_RTC_64BIT selects
 * the rtc_tick_t typedef and AON_RTC_ROUND_TICK width in the authority
 * <driver/aon_rtc_types.h>, and that header is also included by
 * wifi/bk7258_wifi_adapter.c, wifi/hal_port/hw_driver_shim.c,
 * wifi/hal_port/platform_shim.c, aon_pmu/armino aon_pmu_driver.c and the
 * third_party bk_wifi_adapter.c.
 * A module-local definition would give the driver a 64-bit rtc_tick_t and every
 * other translation unit a 32-bit one, silently disagreeing on the alarm_info_t
 * layout and on the published API prototypes.
 ****************************************************************************/

#ifndef __BK7258_AON_RTC_PORT_CONFIG_H
#define __BK7258_AON_RTC_PORT_CONFIG_H

/* Authority BK7258 CP: CONFIG_AON_RTC=y with the 64-bit counter variant and the
 * multi-user alarm list API.  CONFIG_AON_RTC_MANY_USERS gates out the
 * single-owner bk_aon_rtc_create/destroy/tick_init surface in favor of
 * bk_alarm_register/bk_alarm_unregister, matching the imported
 * aon_rtc_driver_64bit.c / aon_rtc_hal_64bit.c pair. */

#define CONFIG_AON_RTC              1
#define CONFIG_AON_RTC_64BIT        1
#define CONFIG_AON_RTC_MANY_USERS   1

/* Authority BK7258 CP: # CONFIG_AON_RTC_DEBUG is not set.  Keeps the ISR trace
 * ring, the timing self-test and the 64-bit arithmetic self-test out of the
 * image.  Tested with #if, so an explicit 0 is correct here. */

#define CONFIG_AON_RTC_DEBUG        0

/* Authority BK7258 CP: CONFIG_SYSTEM_CTRL=y.  Selects the
 * sys_drv_int_group2_enable/disable route for the RTC interrupt, which this
 * port already provides (wifi/hal_port/hw_driver_shim.c:192,197).  Already defined
 * in sys_config.h; guarded so this header can also be read standalone. */

#ifndef CONFIG_SYSTEM_CTRL
#  define CONFIG_SYSTEM_CTRL        1
#endif

/* Authority BK7258 CP: CONFIG_RTC_ANA_WAKEUP_SUPPORT=y.  Compiles
 * bk_rtc_ana_register_wakeup_source(), whose PM enter callback needs
 * sys_drv_rtc_ana_wakeup_enable().  That symbol had no owner in this port, so
 * aon_rtc/nuttx_port/nuttx_port.c supplies the authority register sequence
 * rather than weakening this gate to n. */

#define CONFIG_RTC_ANA_WAKEUP_SUPPORT 1

/* Authority BK7258 CP: # CONFIG_ROSC_COMPENSATION is not set.  Keeps the
 * rosc_32k tick-difference dependency (<driver/rosc_32k.h>,
 * bk_rosc_32k_get_tick_diff) out of the import closure. */

#define CONFIG_ROSC_COMPENSATION    0

/* Authority BK7258 CP: # CONFIG_AON_RTC_DYNAMIC_CLOCK_SUPPORT is not set.
 * bk_rtc_set_clock_freq() keeps its published signature but does not retime an
 * already-running counter. */

#define CONFIG_AON_RTC_DYNAMIC_CLOCK_SUPPORT 0

/* Authority BK7258 CP: # CONFIG_EXTERN_32K is not set.
 *
 * Load-bearing, not cosmetic.  s_aon_rtc_clock_freq is initialized from this
 * macro (aon_rtc_driver_64bit.c:104-108) and is what bk_rtc_get_ms_tick_count()
 * divides by 1000.  Unset selects AON_RTC_DEFAULT_CLOCK_FREQ == 32000, i.e. a
 * rate of 32.0f, which is exactly what this port's previous platform_shim.c
 * implementation returned for a non-X32K LPO source.  libwifi.a's rwnxl_sleep()
 * multiplies that rate by 200.0f to build its poll timeout, so selecting 32768
 * would move the threshold from 6400 to 6553 ticks.
 *
 * The driver tests this with #ifdef, so even `#define CONFIG_EXTERN_32K 0`
 * would select 32768.  It must stay entirely undefined.
 *
 * #define CONFIG_EXTERN_32K        <- must stay commented out
 */

/* CONFIG_FREERTOS_SMP is likewise tested with #ifdef by this driver (it would
 * pull in the Armino spinlock.h and a second critical-section implementation).
 * sys_config.h already omits it deliberately for the same reason; do not add it
 * here either.  The rtos_disable_int/rtos_enable_int critical section that the
 * driver falls back to is provided by wifi/hal_port/rtos_compat_shim.c:264,269.
 *
 * #define CONFIG_FREERTOS_SMP      <- must stay commented out
 */

#endif /* __BK7258_AON_RTC_PORT_CONFIG_H */

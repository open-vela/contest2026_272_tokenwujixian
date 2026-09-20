/* Source-local NuttX ABI boundary for the otherwise unmodified authority
 * AON RTC HAL and driver modules.  Mirrors the established
 * aon_pmu/aon_pmu_armino_types.h boundary: the imported authority sources
 * keep their own include lists, and only the few identifiers that the Armino
 * build injects from outside the module are supplied here.  Behavior stays in
 * the imported authority C and generated LL sources.
 *
 * The AON RTC configuration gate itself is NOT here; it is reached globally
 * through common/sys_config.h so every translation unit that includes
 * <driver/aon_rtc_types.h> agrees on the rtc_tick_t width.  See
 * nuttx_port/include/aon_rtc_port_config.h.
 */

#ifndef __BK7258_AON_RTC_AUTHORITY_TYPES_H
#define __BK7258_AON_RTC_AUTHORITY_TYPES_H

#include <common/bk_include.h>

/* aon_rtc_driver_64bit.c builds its critical section from rtos_disable_int()/
 * rtos_enable_int() but includes only <os/mem.h>, which in this port does not
 * reach <os/os.h>.  Without this both calls are implicit declarations returning
 * int, so the saved interrupt-state flags would round-trip through the wrong
 * type.  wifi/hal_port/rtos_compat_shim.c:264,269 provides the real pair. */
#include <os/os.h>

/* The Armino build injects BK_ASSERT into its sources; aon_rtc_driver_64bit.c
 * uses it (alarm_remove_node, aon_rtc_check_list) without including it.  The
 * glue header maps it onto NuttX DEBUGASSERT.  bk_prelude.h defines the same
 * macro identically under #ifndef, so translation units that see both agree. */
#include <common/bk_assert.h>

/* RTC_INTERRUPT_CTRL_BIT for sys_drv_int_group2_enable/disable.  This is the
 * authority BK7258 sys_types.h already imported with the AON PMU module; a
 * second copy inside this module would duplicate the whole SYS interrupt-bit and
 * PM enum set. */
#include <sys_types.h>

/* Armino places selected hot paths in ITCM.  This port has no such section
 * contract, matching the existing AON PMU / SARADC imports. */
#ifndef __IRAM_SEC
#  define __IRAM_SEC
#endif

#endif /* __BK7258_AON_RTC_AUTHORITY_TYPES_H */

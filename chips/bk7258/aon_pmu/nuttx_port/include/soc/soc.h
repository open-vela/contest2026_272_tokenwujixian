/* NuttX address and register-access boundary for authority AON PMU code. */
#ifndef __BK7258_AON_PMU_NUTTX_SOC_H
#define __BK7258_AON_PMU_NUTTX_SOC_H

#include <stdint.h>

#include <arm_internal.h>
#include <bk7258_memorymap.h>

/* This is the SoC boundary that all directly imported BK7258 authority modules
 * actually resolve `#include <soc/soc.h>` to: it is the first authority
 * nuttx_port include directory on the CP/AP command line, so a same-named
 * per-module header would be shadowed and never compiled.  Keep peripheral
 * bases for every imported module here rather than adding a second copy.
 *
 * Authority reaches the per-peripheral capability headers through
 * soc/bk7258/soc.h -> soc_cap.h.  Only the AON RTC unit count is consumed as a
 * real array dimension (aon_rtc_hal_64bit.h:26 AON_RTC_HAL_UNIT_NUM), so the
 * authority capability header is included verbatim rather than restated.
 * Included by relative path so this boundary does not depend on the order of
 * the per-module authority include directories. */
#include "../../../../aon_rtc/armino/include/soc/bk7258/aon_rtc_cap.h"

/* Same contract for GPIO: SOC_GPIO_NUM and SOC_GPIO_SYSTEM_GROUP_NUM are real
 * array dimensions inside the authority register description
 * (gpio_struct.h:44,133) and GPIO_PERI_FUNC_NUM dimensions the per-pin device
 * table (gpio_hal.h:37), so the authority capability header is included
 * verbatim rather than restated. */
#include "../../../../gpio/armino/include/soc/bk7258/gpio_cap.h"

/* Same contract for the timer block: SOC_TIMER_GROUP_NUM,
 * SOC_TIMER_CHAN_NUM_PER_UNIT and SOC_TIMER_CHAN_NUM_PER_GROUP are consumed as
 * real dimensions by the authority timer LL that sys_hal.c reaches through
 * timer_hal.h. */
#include "../../../../pm/armino/include/soc/bk7258/timer_cap.h"

/* Authority soc/bk7258/soc.h:17 pulls in soc/soc_port.h, which supplies the
 * BK_WHILE / BK_DO_WHILE / BK_WHILE_DO bounded busy-wait contract used by the
 * imported LL headers (timer_ll.h:170, adc_hal.c:173).  CONFIG_BK_WHILE is not
 * set in the authority BK7258 CP config, so the plain loop variants apply and
 * the macros pull in nothing beyond components/log.h. */
#include "../../../../pm/armino/include/soc/soc_port.h"

#define SOC_ADDR_OFFSET       0
#define SOC_SYS_REG_BASE      BK7258_SYS_BASE
#define SOC_AON_PMU_REG_BASE  BK7258_AON_PMU_BASE
#define SOC_AON_RTC_REG_BASE  BK7258_AON_RTC_BASE
#define SOC_SADC_REG_BASE     BK7258_SADC_BASE
#define SOC_TRNG_REG_BASE     BK7258_TRNG_BASE
#define SOC_AON_GPIO_REG_BASE BK7258_AON_GPIO_BASE
#define SOC_OTP_APB_BASE      BK7258_OTP_APB_BASE
#define SOC_OTP_AHB_BASE      BK7258_OTP_AHB_BASE

/* Register access primitives, matching cp/include/soc/bk7258/soc.h:32-70.
 * The bodies route through putreg32()/getreg32() instead of upstream's bare
 * volatile dereference -- that is the NuttX register-access integration point
 * and the only intentional deviation here.  Argument conventions are
 * upstream's and must stay upstream's, because the imported authority code is
 * written against them.
 *
 * REG_GET_BIT takes a MASK, not a bit position (soc.h:40:
 * `*(volatile uint32_t*)(_r) & (_b)`).  This file previously defined it as
 * `(REG_READ(_r) >> (_b)) & 1`, i.e. a bit index -- the same name with
 * incompatible semantics.  Nothing consumed it (every user in this tree calls
 * the separately defined HP_REG_GET_BIT), so it was a latent trap rather than
 * a live bug: the first imported module to use REG_GET_BIT would have read a
 * silently wrong value with no error and no log.  Corrected to upstream's
 * convention (2026-09-09).
 */

#define REG_WRITE(_r, _v) putreg32((uint32_t)(_v), (uintptr_t)(_r))
#define REG_READ(_r)      getreg32((uintptr_t)(_r))
#define REG_GET_BIT(_r, _b) (REG_READ(_r) & (_b))
#define REG_SET_BIT(_r, _b) REG_WRITE((_r), REG_READ(_r) | (_b))
#define REG_CLR_BIT(_r, _b) REG_WRITE((_r), REG_READ(_r) & ~(_b))
#define REG_SET_BITS(_r, _b, _m) \
  REG_WRITE((_r), (REG_READ(_r) & ~(_m)) | ((_b) & (_m)))
#define REG_GET_FIELD(_r, _f) ((REG_READ(_r) >> (_f##_S)) & (_f##_V))
#define REG_SET_FIELD(_r, _f, _v) \
  REG_WRITE((_r), (REG_READ(_r) & ~((_f##_V) << (_f##_S))) | \
                  (((_v) & (_f##_V)) << (_f##_S)))
#define REG_MCHAN_GET_FIELD(_ch, _r, _f) \
  ((REG_READ(_r) >> (_f##_MS(_ch))) & (_f##_V))
#define REG_MCHAN_SET_FIELD(_ch, _r, _f, _v) \
  REG_WRITE((_r), (REG_READ(_r) & ~((_f##_V) << (_f##_MS(_ch)))) | \
                  (((_v) & (_f##_V)) << (_f##_MS(_ch))))

#endif /* __BK7258_AON_PMU_NUTTX_SOC_H */

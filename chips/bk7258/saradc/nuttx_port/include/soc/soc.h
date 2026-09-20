/* NuttX SoC boundary for directly imported BK7258 authority SARADC code. */
#ifndef __BK7258_SARADC_NUTTX_SOC_H
#define __BK7258_SARADC_NUTTX_SOC_H

#include <stdint.h>

#include <arm_internal.h>
#include <bk7258_memorymap.h>

#define SOC_ADDR_OFFSET   0
#define SOC_SADC_REG_BASE BK7258_SADC_BASE
#define SOC_SYS_REG_BASE  BK7258_SYS_BASE
#define SOC_AON_PMU_REG_BASE BK7258_AON_PMU_BASE
/* BK7258 OTP controller bases consumed verbatim by authority otp_ll.h. */
#define SOC_OTP_APB_BASE  BK7258_OTP_APB_BASE
#define SOC_OTP_AHB_BASE  BK7258_OTP_AHB_BASE

#define REG_WRITE(_r, _v) putreg32((uint32_t)(_v), (uintptr_t)(_r))
#define REG_READ(_r)      getreg32((uintptr_t)(_r))
#define REG_GET_BIT(_r, _b) ((REG_READ(_r) >> (_b)) & UINT32_C(1))

#ifndef BIT
#define BIT(i) (UINT32_C(1) << (i))
#endif

/* The hand-written BK_WHILE_DO that used to sit here is removed (2026-09-09):
 * the authority soc/soc_port.h is now imported and reached from the shared
 * boundary in aon_pmu/nuttx_port/include/soc/soc.h.  It never took effect
 * anyway -- that shared header is the first authority nuttx_port include
 * directory on the command line and shadows this one, which is exactly why
 * adc_hal.c:173 was compiling with an implicit declaration of BK_WHILE_DO.
 */

#endif /* __BK7258_SARADC_NUTTX_SOC_H */

/* Compatibility header for the vendored vnd_cal.h.
 *
 * vnd_cal.c is a pure calibration/regulatory data source and uses none of the
 * Armino REG_RD/REG_WR macros. Keep this empty rather than importing
 * Armino's soc/soc.h register namespace into the board dataset.
 */

#ifndef __BK7258_VND_CAL_BK_ARM_ARCH_H
#define __BK7258_VND_CAL_BK_ARM_ARCH_H

/* Upstream bk_arm_arch.h includes soc/soc.h, which in turn pulls in the Beken
 * base typedefs used by vnd_cal.h (UINT8/UINT16/UINT32/INT16/INT32 and the
 * lower-case uint8/uint32 aliases). The calibration source uses no SoC register
 * macro, so include the team replacement typedef leaf directly instead of the
 * whole Armino register namespace.
 */

#include <common/bk_typedef.h>

#endif /* __BK7258_VND_CAL_BK_ARM_ARCH_H */

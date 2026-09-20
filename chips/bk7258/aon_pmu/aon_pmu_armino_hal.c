/* NuttX builds the unmodified authority source below. Its AON API header is
 * intercepted only to avoid the unrelated reduced glue header; declarations
 * come from the source-local authority ABI boundary above. */
#include "aon_pmu_armino_types.h"

#include "armino/middleware/soc/bk7258/hal/aon_pmu_hal.c"

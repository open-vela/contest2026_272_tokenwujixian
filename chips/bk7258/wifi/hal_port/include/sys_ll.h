/*
 * chips/bk7258/wifi/hal_port/include/sys_ll.h
 *
 * Compatibility include for the Armino low-level SYS register accessors.
 *
 * The BK7258 authority header owns these accessors as static inline register
 * operations. Declaring the same names as extern here creates a hard C
 * conflict in sources which include this glue header before hal_port_sys_all.h
 * (for example analog_shim.c and hw_driver_shim.c). It also leaves callers
 * without a real out-of-line provider. Reuse the authority header directly.
 */

#ifndef __BK7258_WIFI_GLUE_SYS_LL_H
#define __BK7258_WIFI_GLUE_SYS_LL_H

#include "../../hal_port/sys_ll.h"

#endif /* __BK7258_WIFI_GLUE_SYS_LL_H */

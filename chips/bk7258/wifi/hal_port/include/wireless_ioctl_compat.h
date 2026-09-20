/* Wireless/private ioctl numbers used by Armino driver_beken.h.
 *
 * Do NOT include NuttX wireless.h here: driver_beken.h carries its own WEXT
 * wire structs (iwreq, iw_event, iw_range, ...). Including both redefines all
 * of them. We only need two integer command bases; preserve upstream's private
 * ABI and let the netdev bridge translate commands later.
 */
#ifndef __BK7258_WIFI_GLUE_WIRELESS_IOCTL_COMPAT_H
#define __BK7258_WIFI_GLUE_WIRELESS_IOCTL_COMPAT_H
#include <nuttx/net/ioctl.h>

#ifndef SIOCIWFIRSTPRIV
#  define SIOCIWFIRSTPRIV 0x8BE0
#endif
#ifndef SIOCDEVPRIVATE
#  define SIOCDEVPRIVATE 0x89F0
#endif
#endif

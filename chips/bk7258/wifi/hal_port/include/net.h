/* Bare-name forwarder: rw_msg_rx.c:48 and wifi_wpa_cmd.c:135 include "net.h",
 * which upstream resolves to lwip-2.1.2/port/net.h because that port directory
 * is on the include path. Our bridge version of that header lives at
 * lwip_intf_v2_1/lwip-2.1.2/port/net.h (see _relpath_anchor/README.md for why
 * it sits at that path); this keeps a single definition.
 *
 * NuttX has no <net.h>, so this bare name shadows nothing.
 */

#include "lwip_intf_v2_1/lwip-2.1.2/port/net.h"

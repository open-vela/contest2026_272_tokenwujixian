/* Bare-name forwarder: rwnx_utils.c:18 includes "netif.h", which upstream
 * resolves to lwip-2.1.2/src/include/lwip/netif.h because that directory is on
 * the include path. Our bridge version lives at lwip/netif.h; this keeps a
 * single definition.
 *
 * NuttX has no <netif.h>, so this bare name shadows nothing.
 */

#include "lwip/netif.h"

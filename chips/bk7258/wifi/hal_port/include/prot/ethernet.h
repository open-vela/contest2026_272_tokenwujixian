/* Bare-name forwarder: rwnx_rx.c:25 includes "prot/ethernet.h", which upstream
 * resolves because lwip-2.1.2/src/include/lwip is itself on the include path.
 * Our version lives at lwip/prot/ethernet.h; this keeps a single definition.
 */

#include "lwip/prot/ethernet.h"

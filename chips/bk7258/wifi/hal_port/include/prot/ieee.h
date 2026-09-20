/* Bare-name forwarder: rwnx_tx.c includes "prot/ieee.h", which upstream
 * resolves because lwip-2.1.2/src/include/lwip is itself on the include path.
 * Our version lives at lwip/prot/ieee.h; this keeps a single definition.
 */

#include "lwip/prot/ieee.h"

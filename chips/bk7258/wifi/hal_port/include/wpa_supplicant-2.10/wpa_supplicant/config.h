/* Path-shim for upstream's component-relative include, same mechanism as the
 * wpa_supplicant_i.h shim next to this file: wifi_v2.c:36 writes
 *
 *   #include "../../wpa_supplicant-2.10/wpa_supplicant/config.h"
 *
 * which upstream resolves because components/ is two levels above
 * components/bk_wifi/src/. See ../../_relpath_anchor/README.md.
 *
 * Forwarding only; the vendored header stays untouched.
 */

#include "../../../armino/wpa_supplicant/wpa_supplicant/config.h"

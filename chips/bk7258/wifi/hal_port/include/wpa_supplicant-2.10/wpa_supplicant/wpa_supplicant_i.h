/* Path-shim for upstream's component-relative include.
 *
 * wifi_v2.c:35,52 reach wpa_supplicant's internal header by climbing out of
 * their own component:
 *
 *   #include "../../wpa_supplicant-2.10/wpa_supplicant/wpa_supplicant_i.h"
 *
 * Upstream that resolves because the file lives in
 * components/bk_wifi/src/, so ../.. lands on components/, where
 * wpa_supplicant-2.10 is a sibling. In our layout the vendored tree is
 * armino/wpa_supplicant/, so the quoted path misses.
 *
 * A quoted include falls back to the -I list after the including file's own
 * directory, and hal_port/include/_relpath_anchor/level2 is on that list, so
 * level2/../../wpa_supplicant-2.10/... lands right here. Same mechanism as
 * lwip_intf_v2_1/lwip-2.1.2/port/net.h -- see _relpath_anchor/README.md.
 *
 * This file only forwards: the header itself stays vendored, untouched.
 */

#include "../../../armino/wpa_supplicant/wpa_supplicant/wpa_supplicant_i.h"

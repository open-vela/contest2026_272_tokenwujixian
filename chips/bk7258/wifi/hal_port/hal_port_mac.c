/* hal_port_mac.c - base/derived MAC address service, ported from Armino.
 *
 * Source: cp/components/bk_system/mac.c
 *           :39   DEFAULT_MAC_ADDR
 *           :373  is_valid_mac_addr
 *           :423  mac_init
 *           :466  bk_get_mac
 *           :486+ bk_set_base_mac
 *
 * Why this replaces the previous shim (hal_port/system_shim.c):
 *
 * The old implementation accepted ONLY MAC_TYPE_BASE and returned BK_FAIL for
 * every other type WITHOUT writing the caller's buffer:
 *
 *     if (mac == NULL || type != MAC_TYPE_BASE) return BK_FAIL;
 *
 * The closed library asks for MAC_TYPE_STA through
 * bk_wifi_sta_get_mac() (wifi_v2.c:4481), which DISCARDS the return value and
 * unconditionally reports BK_OK.  So every consumer received uninitialised
 * stack memory while being told the call succeeded:
 *   - scan_probe_req_tx (two calls, per the authority disassembly)
 *   - rw_msg_send_add_if -> MM_ADD_IF_REQ payload (rw_msg_tx.c:568)
 *   - g_sta_param_ptr->own_mac (wifi_v2.c:665, :2714, :2725)
 *   - drv->own_addr (driver_beken.c:2121)
 *
 * Board evidence: NXMAC's own address registers read zero on our board
 * (0x49100010 = 0, 0x49100014 = 0) where the authority holds a real address
 * (0x468c47c8 / 0x00001502 = C8:47:8C:46:02:15 -- the C8:47:8C Beken OUI,
 * matching DEFAULT_MAC_ADDR's first three bytes with the low bytes
 * randomised).  A zero address filter cannot accept the unicast probe
 * responses a scan depends on.
 *
 * Deviations from the Armino original, and why:
 *
 * 1. mac_init's persistence chain (get_net_info / read_base_mac_to_otp2 /
 *    save_net_info) is replaced by the board's flash accessor
 *    bk7258_wifi_board_get_mac().  Our save_info_item/get_info_item are still
 *    stubs (hal_port/net_param_shim.c), so there is nothing to persist into yet.
 * 2. Armino randomises the low bytes when no stored address is found
 *    (CONFIG_RANDOM_MAC_ADDR=1 authoritatively) and persists the result.
 *    Without a persistence layer a random address would change every boot,
 *    which makes board logs non-reproducible mid-investigation, so this port
 *    falls back to the DEFAULT_MAC_ADDR constant verbatim instead: it is
 *    deterministic, unicast, non-zero and carries the same OUI.  Revisit once
 *    net_info persistence is real.
 * 3. CONFIG_CUS_MAC_MASK is 0x0 in the authority build, so the AP branch there
 *    compiles to the NX_VIRT_DEV_MAX path; that is the variant ported here.
 *    Written without #if on config symbols our sys_config.h does not define,
 *    to avoid -Wundef noise.
 */

#include <stdint.h>
#include <string.h>
#include <nuttx/config.h>
#include <syslog.h>

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <components/system.h>

#include "bk7258_wifi_internal.h"

/* mac.c:39 */

static const uint8_t g_default_mac[BK_MAC_ADDR_LEN] =
{
  0xc8, 0x47, 0x8c, 0x00, 0x00, 0x18
};

/* mac.c:40-41 */

static uint8_t g_base_mac[BK_MAC_ADDR_LEN];
static bool g_mac_inited;

/* mac.c:373 -- all-FF and all-zero are both rejected. */

static bool hp_mac_is_valid(const uint8_t *mac)
{
  unsigned int i;
  bool all_ff = true;
  bool all_zero = true;

  for (i = 0; i < BK_MAC_ADDR_LEN; i++)
    {
      all_ff &= mac[i] == 0xff;
      all_zero &= mac[i] == 0x00;
    }

  return !all_ff && !all_zero;
}

/* mac.c:423 mac_init, with the persistence chain replaced (see header note). */

static void hp_mac_init(void)
{
  uint8_t candidate[BK_MAC_ADDR_LEN];

  memcpy(g_base_mac, g_default_mac, BK_MAC_ADDR_LEN);

  if (bk7258_wifi_board_get_mac(candidate) == 0 &&
      hp_mac_is_valid(candidate) && !BK_IS_GROUP_MAC(candidate))
    {
      memcpy(g_base_mac, candidate, BK_MAC_ADDR_LEN);
      syslog(LOG_INFO, "[BK7258-WIFI] mac: flash %02x:%02x:%02x:%02x:%02x:%02x\n",
             g_base_mac[0], g_base_mac[1], g_base_mac[2],
             g_base_mac[3], g_base_mac[4], g_base_mac[5]);
    }
  else
    {
      /* Loud on purpose: running on the vendor default means every board
       * flashed with this image shares one address. */

      syslog(LOG_WARNING,
             "[BK7258-WIFI] mac: no valid flash MAC, using default "
             "%02x:%02x:%02x:%02x:%02x:%02x\n",
             g_base_mac[0], g_base_mac[1], g_base_mac[2],
             g_base_mac[3], g_base_mac[4], g_base_mac[5]);
    }
}

/* mac.c:466 -- every type derives from the base address, and every arm writes
 * the caller's buffer.  That is the whole point of the port: the library
 * ignores the return code, so returning an error without filling the buffer
 * is indistinguishable from success to it. */

bk_err_t bk_get_mac(uint8_t *mac, mac_type_t type)
{
  uint8_t mac_mask;
  uint8_t mac_low;

  if (mac == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  if (!g_mac_inited)
    {
      hp_mac_init();
      g_mac_inited = true;
    }

  switch (type)
    {
      case MAC_TYPE_BASE:
        memcpy(mac, g_base_mac, BK_MAC_ADDR_LEN);
        break;

      case MAC_TYPE_STA:
        memcpy(mac, g_base_mac, BK_MAC_ADDR_LEN);
        break;

      case MAC_TYPE_AP:

        /* NX_VIRT_DEV_MAX == 2, so mask == 1: AP and STA must agree on
         * bytes 0-4 and on byte5[7:2], differing only in byte5[1:0]. */

        mac_mask = (uint8_t)(0xff & (2 - 1));
        memcpy(mac, g_base_mac, BK_MAC_ADDR_LEN);
        mac_low = mac[5];
        mac[5] &= (uint8_t)~mac_mask;
        mac_low = (uint8_t)((mac_low & mac_mask) ^ mac_mask);
        mac[5] |= mac_low;
        break;

      case MAC_TYPE_BLUETOOTH:
        memcpy(mac, g_base_mac, BK_MAC_ADDR_LEN);
        mac[5] += 1;
        break;

      case MAC_TYPE_ETH:

        /* NX_VIRT_DEV_MAX + BLUETOOTH */

        memcpy(mac, g_base_mac, BK_MAC_ADDR_LEN);
        mac[5] += 2 + 1;
        break;

      case MAC_TYPE_P2P:

        /* Locally-administered bit of byte0, as a phone derives its P2P
         * device address from the station MAC. */

        memcpy(mac, g_base_mac, BK_MAC_ADDR_LEN);
        mac[0] ^= 0x02;
        break;

      default:
        return BK_ERR_INVALID_MAC_TYPE;
    }

  return BK_OK;
}

/* mac.c:486 -- the net_info persistence half is intentionally absent (our
 * save_info_item is a stub), so this updates the live base address only. */

bk_err_t bk_set_base_mac(const uint8_t *mac)
{
  if (mac == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  if (BK_IS_GROUP_MAC(mac))
    {
      syslog(LOG_ERR, "[BK7258-WIFI] mac: refusing bc/mc address\n");
      return BK_ERR_GROUP_MAC;
    }

  memcpy(g_base_mac, mac, BK_MAC_ADDR_LEN);
  g_mac_inited = true;
  return BK_OK;
}

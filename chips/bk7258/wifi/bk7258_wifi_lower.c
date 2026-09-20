/*
 * chips/bk7258/wifi/bk7258_wifi_lower.c
 *
 * NuttX netdev_lowerhalf data/control plane for BK7258 Wi-Fi (STA MVP).
 *
 * Skeleton status: the ops tables and register/carrier plumbing are wired;
 * transmit/receive and the wireless_ops_s handlers return documented
 * errors until the Beken public API (bk_wifi_sta_*, bk_wifi_scan_*) and the
 * vendor MAC TX/RX entry points are connected by the integration branch.
 */

#include <nuttx/config.h>
#include <nuttx/kmalloc.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/wireless/wireless.h>

/* getreg32 is a NuttX macro here (arm_internal.h), not a function; the
 * scan-done diagnostics read the NX MAC FSM registers directly. */
#include <arm_internal.h>
#include <bk7258_memorymap.h>

#include <errno.h>
#include <net/if_arp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "lwip/pbuf.h"
#include "generated/lmac_bus_msg.h"
#include "rwnx_intf.h"
#include "bk_private/bk_wifi.h"
#include "common/bk_err.h"

#include "generated/lmac_wifi_adapter.h"

#include "bk7258_wifi_internal.h"
#include "bk7258_scan_diag.h"
/* up_udelay: the clktick sample needs a real interval between the two
 * reads, not two adjacent loads. */
#include <nuttx/arch.h>

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
#include <components/event.h>
#include <components/sensor.h>
#include <modules/wifi.h>
#include <modules/wifi_types.h>
#include "bk_phy_adapter.h"
#include "bk_rf_adapter.h"
#include <syslog.h>

/* bk_aon_rtc_driver_init()/bk_aon_rtc_get_current_tick() and AON_RTC_ID_1, from
 * the authority AON RTC module imported under chips/bk7258/aon_rtc/. */

#include <driver/aon_rtc.h>

/* ke_l2_packet_tx() and struct ke_sk_params, the supplicant-side entry. */

#include <wpa_compat/sk_intf.h>

/* Ethernet header layout for the EAPOL split in ethernetif_input().
 *
 * Spelled out locally instead of including "lwip/prot/ethernet.h": that header
 * pulls in lwip/prot/ieee.h, which declares ETHTYPE_* as enum members, while
 * NuttX's nuttx/net/ethernet.h:55-56 defines ETHTYPE_ARP and ETHTYPE_IP as
 * macros -- and that header is already in scope here through
 * nuttx/net/netdev.h.  The macros then expand inside the enum
 * ("ETHERTYPE_ARP = 0x0806u") and the whole enum fails to parse.  The vendored
 * glue can use the lwIP header only because it never includes NuttX net
 * headers.
 *
 * Reading the two ethertype bytes by hand also keeps the comparison in network
 * byte order, so no htons() and no <arpa/inet.h> are needed, and it does not
 * assume the payload is 2-byte aligned.  ETH_PAD_SIZE is 0 in this port, so the
 * 802.3 header is exactly dest[6] + src[6] + type[2].
 */

#define BK7258_ETH_HDR_LEN      14u
#define BK7258_ETH_SRC_OFFSET    6u
#define BK7258_ETH_TYPE_OFFSET  12u
#define BK7258_ETHTYPE_EAPOL    0x888eu
#endif

/* WPA2 passphrase length bounds (wifi_types.h:67 WIFI_PASSWORD_LEN == 64+1).
 * A passphrase is 8..63 characters, or exactly 64 hex digits for a raw PMK. */

#define BK7258_WIFI_PSK_MIN_LEN  8u

extern int bmsg_tx_sender(struct pbuf *p, uint32_t vif_idx);

/* os/os.h defines this as beken_thread_t *; beken_thread_t is void *. Keep
 * the declaration local so this NuttX lower-half does not import its broad
 * compatibility macro surface merely to obtain the SDK scan request token. */

extern void **rtos_get_current_thread(void);
extern uint32_t bk7258_wifi_pwd_ofdm_get_override(void);

/****************************************************************************
 * Private data
 ****************************************************************************/

static struct bk7258_wifi_s g_bk7258_wifi;
static atomic_int g_bk7258_wifi_init_state = ATOMIC_VAR_INIT(0);
static int g_bk7258_wifi_init_result;

enum bk7258_wifi_init_state_e
{
  BK7258_WIFI_INIT_NOT_STARTED = 0,
  BK7258_WIFI_INIT_INITIALIZING,
  BK7258_WIFI_INIT_READY,
  BK7258_WIFI_INIT_FAILED,
};

#define PRIV2LOWER(p) (&(p)->lower)

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
static bk_err_t bk7258_wifi_scan_done(void *arg, event_module_t module,
                                      int event_id, void *event_data);
static bk_err_t bk7258_wifi_sta_connected(void *arg, event_module_t module,
                                          int event_id, void *event_data);
static bk_err_t bk7258_wifi_sta_disconnected(void *arg, event_module_t module,
                                             int event_id, void *event_data);
#endif

void bk7258_wifi_lower_rx_submit(struct bk7258_wifi_s *priv,
                                 struct bk7258_vpkt *vpkt);

static int bk7258_wifi_rx_enqueue(struct bk7258_wifi_s *priv,
                                  struct iob_s *iob)
{
  irqstate_t flags;

  flags = enter_critical_section();
  iob->io_flink = NULL;
  if (priv->rx_tail != NULL)
    {
      priv->rx_tail->io_flink = iob;
    }
  else
    {
      priv->rx_head = iob;
    }

  priv->rx_tail = iob;
  leave_critical_section(flags);
  return 0;
}

/* Armino's connector callback for an Ethernet frame. The vendor pbuf is
 * copied into the NuttX queue and released only after the copy completes.
 *
 * EAPOL is split off before that copy, see the block below.
 */

void ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx)
{
  struct bk7258_vpkt *vpkt;
  struct pbuf *q;
  uint8_t *dst;
  (void)iface;
  (void)dst_idx;

  if (p == NULL || p->tot_len == 0 || p->tot_len > BK7258_WIFI_FRAME_MAX)
    {
      if (p != NULL)
        {
          pbuf_free(p);
        }
      return;
    }

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  /* Hand EAPOL to wpa_supplicant instead of to the NuttX stack.
   *
   * Transcribed from the vendor's own bridge, ethernetif_input() in
   * cp/components/lwip_intf_v2_1/lwip-2.1.2/port/wlanif.c:331 -- there the
   * EAPOL test is likewise the first thing done to a frame, ahead of the netif
   * lookup, with this comment: "EAPOL must reach wpa_supplicant even when lwip
   * netif is not registered yet".
   *
   * Why it has to be here: the 4-way handshake is carried in 802.3 frames with
   * ethertype 0x888E, and every 802.3 frame arrives through this one function
   * (rwnx_rx.c:728, the tail of rwm_upload_data(), which is registered as
   * g_rwnx_connector.data_outbound_func in rw_ieee80211.c).  Without this
   * split, M1 was copied into the NuttX RX queue, where the network stack has
   * no consumer for 0x888E and drops it: wpa_supplicant reached ASSOCIATED,
   * armed its 10 s auth timeout, never saw M1, and the AP disassociated us
   * with reason 15 (4WAY_HANDSHAKE_TIMEOUT) about four seconds later.  Board-
   * verified three times per run in the sta-hwioctlfix image.
   *
   * Management frames were never affected, which is why auth/assoc/beacon all
   * worked: those take the separate rwnx_rx_mgmt_any() path.
   *
   * This gap was known and recorded, not newly discovered:
   * hal_port/include/wpa_compat/sk_intf.h:20-22 states that "EAPOL/WAI (0x888e)
   * demux to the WPA entity is the network bridge's job (plan 11.4.2), not this
   * header's" -- the bridge is this function, and the work was never done.
   *
   * The self-echo filter is the vendor's too: our own transmitted EAPOL can be
   * looped back by the MAC, and feeding it to the supplicant would corrupt the
   * handshake state machine.  Frames whose source is our own address are
   * dropped rather than forwarded.
   *
   * Runtime-gated because ke_l2_packet_tx() lives in sk_intf.c, which is only
   * compiled under CONFIG_BK7258_WIFI_VENDOR_RUNTIME (CMakeLists.txt:158); the
   * runtime-disabled CP image must not reference it.
   */

  if (p->len > BK7258_ETH_HDR_LEN)
    {
      FAR const uint8_t *eth = (FAR const uint8_t *)p->payload;
      uint16_t ethtype = ((uint16_t)eth[BK7258_ETH_TYPE_OFFSET] << 8) |
                          (uint16_t)eth[BK7258_ETH_TYPE_OFFSET + 1u];

      if (ethtype == BK7258_ETHTYPE_EAPOL)
        {
          struct ke_sk_params params;
          uint8_t own_mac[6];

          /* The vendor bridge compares the frame's source against the vif's
           * in-RAM MAC (wlanif.c: wifi_netif_vif_to_mac(vif)).  Our equivalent
           * is the vendor's own accessor: it resolves to bk_get_mac(), which
           * caches the address in RAM after one flash read at first use
           * (hal_port_mac.c:126-137) -- so this stays cheap on the RX path.
           * bk7258_wifi_board_get_mac() was rejected for exactly that reason:
           * it re-reads the flash partition on every call, here inside the
           * core thread during the 4-way window. */

          if (bk_wifi_sta_get_mac(own_mac) == BK_OK &&
              memcmp(own_mac, eth + BK7258_ETH_SRC_OFFSET,
                     sizeof(own_mac)) == 0)
            {
              pbuf_free(p);
              return;
            }

          /* buf/len describe the whole 802.3 frame, header included, exactly
           * as the vendor bridge passes it. */

          params.buf  = (unsigned char *)p->payload;
          params.len  = p->len;
          params.flag = iface;
          params.freq = 0;

          ke_l2_packet_tx(&params);
          pbuf_free(p);
          return;
        }
    }
#endif /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

  vpkt = bk7258_vpkt_alloc(p->tot_len, true);
  if (vpkt == NULL)
    {
      pbuf_free(p);
      return;
    }

  dst = vpkt->payload;
  for (q = p; q != NULL; q = q->next)
    {
      memcpy(dst, q->payload, q->len);
      dst += q->len;
    }
  bk7258_wifi_lower_rx_submit(&g_bk7258_wifi, vpkt);
  pbuf_free(p);
}

/****************************************************************************
 * Data plane
 *
 * transmit/receive stay disconnected until the vendor MAC TX/RX entry points
 * are wired. Returning -ENOSYS from transmit makes the upper half recycle the
 * NetPKT; returning NULL from receive signals "no packet available".
 ****************************************************************************/

static int bk7258_wifi_ifup(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv =
    (FAR struct bk7258_wifi_s *)lower;

  /* Administrative up only.  Carrier is owned by the association events
   * (bk7258_wifi_sta_connected/disconnected) and must not be touched here: an
   * interface is legitimately brought up before associating, and clearing the
   * flag on ifup would desync this mirror from the IFF_RUNNING bit that
   * netdev_carrier_on/off actually own -- including the case where ifconfig up
   * follows a successful connect. */

  (void)priv;
  return OK;
}

static int bk7258_wifi_ifdown(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv =
    (FAR struct bk7258_wifi_s *)lower;

  /* Administrative down only; carrier is owned by the association events for
   * the same reason as in ifup above.  Taking the interface down does not
   * disassociate, so the link -- and therefore the carrier -- outlives it. */

  (void)priv;
  return OK;
}

/* Hand a filled vendor pbuf to the vendor TX queue and release our reference.
 *
 * Split out so the netdev data path and the bring-up TX probe share the one
 * piece with non-obvious semantics: bmsg_tx_sender() takes its own reference
 * and drops it again if the queue push fails, so the reference from
 * pbuf_alloc() is ours to release on every path.  The two callers differ only
 * in where the bytes come from, which is why the allocation stays with them.
 */

static int bk7258_wifi_tx_pbuf(FAR struct bk7258_wifi_s *priv,
                               struct pbuf *p)
{
  int ret = bmsg_tx_sender(p, priv->vif_idx);

  pbuf_free(p);

  if (ret != BK_OK)
    {
      /* The low-heap early return in bmsg_tx_sender() is the one case that is
       * not a transport failure, and the upper half retries on -ENOMEM. */

      return ret == BK_ERR_NO_MEM ? -ENOMEM : -EIO;
    }

  return OK;
}

static int bk7258_wifi_transmit(FAR struct netdev_lowerhalf_s *lower,
                                FAR netpkt_t *pkt)
{
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  struct pbuf *p;
  unsigned int len;
  int ret;

  len = netpkt_getdatalen(lower, pkt);
  if (len == 0 || len > BK7258_WIFI_FRAME_MAX)
    {
      return -EMSGSIZE;
    }

  p = pbuf_alloc(PBUF_RAW_TX, (u16_t)len, PBUF_RAM);
  if (p == NULL)
    {
      return -ENOMEM;
    }

  ret = netpkt_copyout(lower, p->payload, pkt, len, 0);
  if (ret < 0)
    {
      pbuf_free(p);
      return ret;
    }

  ret = bk7258_wifi_tx_pbuf(priv, p);
  if (ret < 0)
    {
      return ret;
    }

  netpkt_free(lower, pkt, NETPKT_TX);
  netdev_lower_txdone(lower);
  return OK;
}

static FAR netpkt_t *bk7258_wifi_receive(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  FAR struct iob_s *iob;
  irqstate_t flags = enter_critical_section();

  iob = priv->rx_head;
  if (iob != NULL)
    {
      priv->rx_head = iob->io_flink;
      if (priv->rx_head == NULL)
        {
          priv->rx_tail = NULL;
        }
      iob->io_flink = NULL;
    }

  leave_critical_section(flags);
  return iob;
}

static void bk7258_wifi_reclaim(FAR struct netdev_lowerhalf_s *lower)
{
  /* Genuinely nothing to reclaim, rather than a stub.
   *
   * netdev_upper_can_tx() (netdev_upperhalf.c:248-256) calls this when the TX
   * quota has run out, giving a driver the chance to walk its hardware
   * descriptors and release the netpkts it still owns.  This driver never owns
   * one past the transmit call: bk7258_wifi_transmit() copies the payload into a
   * vendor pbuf, hands that to bmsg_tx_sender(), and frees the netpkt before
   * returning, so quota is returned on the same call stack that consumed it.
   *
   * Kept in netdev_ops_s regardless.  Without it can_tx() has no recovery path
   * at all, and quota loss would then be permanently fatal instead of merely
   * costing one poll cycle. */
}

/****************************************************************************
 * Control plane
 *
 * WAPI/WEXT STA handlers.  The standard ioctl sequence is:
 *   wapi mode  -> SIOCSIWMODE   -> ops->mode
 *   wapi psk   -> SIOCSIWAUTH x2 -> ops->auth  (WPA version, cipher)
 *              -> SIOCSIWENCODEEXT -> ops->passwd
 *   wapi essid -> SIOCSIWESSID ON -> ops->essid then ops->connect
 *   wapi scan  -> SIOCSIWSCAN   -> ops->scan   (existing, unchanged)
 ****************************************************************************/

static int bk7258_wifi_connect(FAR struct netdev_lowerhalf_s *lower)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;

  if (!bk7258_wifi_is_ready())
    {
      return -ENODEV;
    }

  if (priv->sta_mode != IW_MODE_INFRA)
    {
      return -EOPNOTSUPP;
    }

  if (priv->sta_ssid_len == 0u)
    {
      return -EINVAL;
    }

  /* Auth combination must be internally consistent before reaching the
   * vendor.  Open = WPA disabled + no PSK.  WPA2-PSK = WPA2 + CCMP +
   * PSK present.  Anything else is a caller error. */

  if (priv->auth_wpa == IW_AUTH_WPA_VERSION_DISABLED)
    {
      if (priv->sta_psk_len != 0u)
        {
          return -EINVAL;
        }
    }
  else if (priv->auth_wpa == IW_AUTH_WPA_VERSION_WPA2)
    {
      if (priv->auth_cipher != IW_AUTH_CIPHER_CCMP ||
          priv->sta_psk_len < BK7258_WIFI_PSK_MIN_LEN)
        {
          return -EINVAL;
        }
    }
  else
    {
      return -EOPNOTSUPP;
    }

  syslog(LOG_INFO, "[BK7258-WIFI] wapi connect: ssid=\"%s\" psk_len=%u\n",
         priv->sta_ssid, (unsigned)priv->sta_psk_len);

  return bk7258_wifi_sta_connect(priv->sta_ssid, priv->sta_psk);
#else
  (void)lower;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_disconnect(FAR struct netdev_lowerhalf_s *lower)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  bk_err_t ret;

  (void)lower;

  if (!bk7258_wifi_is_ready())
    {
      return -ENODEV;
    }

  ret = bk_wifi_sta_disconnect();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] wapi disconnect failed=%d\n", (int)ret);
      return -EIO;
    }

  return OK;
#else
  (void)lower;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_essid(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  FAR char *dst;
  FAR uint8_t *dst_len;
  size_t len;

  if (iwr == NULL)
    {
      return -EINVAL;
    }

  if (!set)
    {
      /* Return the connection SSID if one is recorded, otherwise the
       * directed-scan SSID.  Both are NuttX-owned copies. */

      dst = priv->sta_ssid_len > 0u ? priv->sta_ssid : priv->scan_ssid;
      len = priv->sta_ssid_len > 0u ? priv->sta_ssid_len :
                                      priv->scan_ssid_len;
      if (iwr->u.essid.pointer == NULL || iwr->u.essid.length < len)
        {
          return -EINVAL;
        }

      memcpy(iwr->u.essid.pointer, dst, len);
      iwr->u.essid.length = (uint16_t)len;
      iwr->u.essid.flags = len > 0u ? 1u : 0u;
      return OK;
    }

  /* The upper-half routes IW_ESSID_ON here then calls connect(); it routes
   * IW_ESSID_DELAY_ON here without connecting (directed-scan selection).
   * IW_ESSID_OFF never reaches this handler -- the upper-half calls
   * disconnect() directly. */

  if (iwr->u.essid.pointer == NULL)
    {
      return -EINVAL;
    }

  /* ON records the connection SSID; DELAY_ON records the scan SSID. */

  if (iwr->u.essid.flags == IW_ESSID_ON)
    {
      dst = priv->sta_ssid;
      dst_len = &priv->sta_ssid_len;
    }
  else
    {
      dst = priv->scan_ssid;
      dst_len = &priv->scan_ssid_len;
    }

  len = iwr->u.essid.length;

  /* Some WEXT callers include the terminator in length; drop it. */

  if (len > 0u && ((FAR const char *)iwr->u.essid.pointer)[len - 1u] == '\0')
    {
      len--;
    }

  if (len > BK7258_WIFI_SCAN_SSID_LEN)
    {
      return -EINVAL;
    }

  memcpy(dst, iwr->u.essid.pointer, len);
  dst[len] = '\0';
  *dst_len = (uint8_t)len;
  return OK;
#else
  return -ENOSYS;
#endif
}

static int bk7258_wifi_bssid(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_passwd(FAR struct netdev_lowerhalf_s *lower,
                              FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  FAR struct iw_encode_ext *ext;

  if (iwr == NULL || iwr->u.encoding.pointer == NULL)
    {
      return -EINVAL;
    }

  ext = (FAR struct iw_encode_ext *)iwr->u.encoding.pointer;

  if (!set)
    {
      /* Report whether a key is configured and its algorithm, never the
       * key bytes themselves. */

      ext->alg = priv->sta_psk_len > 0u ? IW_ENCODE_ALG_CCMP :
                                          IW_ENCODE_ALG_NONE;
      ext->key_len = 0;
      return OK;
    }

  switch (ext->alg)
    {
      case IW_ENCODE_ALG_NONE:
        memset(priv->sta_psk, 0, sizeof(priv->sta_psk));
        priv->sta_psk_len = 0;
        return OK;

      case IW_ENCODE_ALG_CCMP:
        break;

      default:
        return -EOPNOTSUPP;
    }

  if (ext->key_len < BK7258_WIFI_PSK_MIN_LEN ||
      ext->key_len >= sizeof(priv->sta_psk))
    {
      return -EINVAL;
    }

  memcpy(priv->sta_psk, ext->key, ext->key_len);
  priv->sta_psk[ext->key_len] = '\0';
  priv->sta_psk_len = (uint8_t)ext->key_len;
  return OK;
#else
  (void)lower;
  (void)iwr;
  (void)set;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_mode(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;

  if (iwr == NULL)
    {
      return -EINVAL;
    }

  if (!set)
    {
      iwr->u.mode = priv->sta_mode;
      return OK;
    }

  if (iwr->u.mode != IW_MODE_INFRA)
    {
      return -EOPNOTSUPP;
    }

  priv->sta_mode = IW_MODE_INFRA;
  return OK;
#else
  (void)lower;
  (void)iwr;
  (void)set;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_auth(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  int idx;

  if (iwr == NULL)
    {
      return -EINVAL;
    }

  idx = iwr->u.param.flags & IW_AUTH_INDEX;

  if (!set)
    {
      switch (idx)
        {
          case IW_AUTH_WPA_VERSION:
            iwr->u.param.value = (int32_t)priv->auth_wpa;
            return OK;
          case IW_AUTH_CIPHER_PAIRWISE:
            iwr->u.param.value = (int32_t)priv->auth_cipher;
            return OK;
          default:
            return -EOPNOTSUPP;
        }
    }

  switch (idx)
    {
      case IW_AUTH_WPA_VERSION:
        if (iwr->u.param.value != IW_AUTH_WPA_VERSION_DISABLED &&
            iwr->u.param.value != IW_AUTH_WPA_VERSION_WPA2)
          {
            return -EOPNOTSUPP;
          }
        priv->auth_wpa = (uint32_t)iwr->u.param.value;
        return OK;

      case IW_AUTH_CIPHER_PAIRWISE:
        if (iwr->u.param.value != IW_AUTH_CIPHER_NONE &&
            iwr->u.param.value != IW_AUTH_CIPHER_CCMP)
          {
            return -EOPNOTSUPP;
          }
        priv->auth_cipher = (uint32_t)iwr->u.param.value;
        return OK;

      default:
        return -EOPNOTSUPP;
    }
#else
  (void)lower;
  (void)iwr;
  (void)set;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_country(FAR struct netdev_lowerhalf_s *lower,
                               FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_scan(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  size_t required = 0;
  size_t ssid_len;
  uint8_t index;
  int ret;

  if (!priv->scan_lock_ready)
    {
      return -ENODEV;
    }

  if (set)
    {
      nxmutex_lock(&priv->scan_lock);
      if (priv->scan_in_progress)
        {
          nxmutex_unlock(&priv->scan_lock);
          return -EBUSY;
        }

      /* bk_wifi_scan_start() records this exact caller token in its scan
       * request, then returns it in wifi_event_scan_done_t.scan_id. */

      priv->scan_id = (uint32_t)(uintptr_t)rtos_get_current_thread();
      priv->scan_in_progress = true;
      priv->scan_complete = false;
      priv->scan_status = -EAGAIN;
      priv->scan_count = 0;
      nxmutex_unlock(&priv->scan_lock);

      bk7258_scan_diag_begin();

      /* Directed vs broadcast scan.  The authority reference run was a
       * DIRECTED scan (`scanu_start_req ... ssid_len=3`) while ours has always
       * been broadcast (`ssid_len=0`); aligning that removes one difference
       * from the comparison.  Config built exactly like the vendor's own
       * callers do (zeroed struct, ssid copied, everything else left 0). */

      if (priv->scan_ssid_len > 0u)
        {
          wifi_scan_config_t cfg;

          memset(&cfg, 0, sizeof(cfg));
          memcpy(cfg.ssid, priv->scan_ssid, priv->scan_ssid_len);
          syslog(LOG_INFO, "[BK7258-WIFI] scan: directed len=%u\n",
                 (unsigned)priv->scan_ssid_len);
          ret = bk_wifi_scan_start(&cfg);
        }
      else
        {
          ret = bk_wifi_scan_start(NULL);
        }

      if (ret != BK_OK)
        {
          bk7258_scan_diag_record(BK7258_SCAN_DIAG_START_REJECTED);
          nxmutex_lock(&priv->scan_lock);
          priv->scan_in_progress = false;
          priv->scan_complete = true;
          priv->scan_status = ret == BK_ERR_BUSY ? -EBUSY : -EIO;
          nxmutex_unlock(&priv->scan_lock);
          return priv->scan_status;
        }

      bk7258_scan_diag_record(BK7258_SCAN_DIAG_START_ACCEPTED);

      return OK;
    }

  if (iwr == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->scan_lock);
  if (priv->scan_in_progress)
    {
      nxmutex_unlock(&priv->scan_lock);
      return -EAGAIN;
    }

  if (!priv->scan_complete)
    {
      nxmutex_unlock(&priv->scan_lock);
      return -EINVAL;
    }

  if (priv->scan_status < 0)
    {
      ret = priv->scan_status;
      nxmutex_unlock(&priv->scan_lock);
      return ret;
    }

  /* Calculate the full WEXT event stream before writing user storage. This
   * avoids returning partial results or exposing an incomplete AP record. */

  for (index = 0; index < priv->scan_count; index++)
    {
      ssid_len = strnlen(priv->scan_aps[index].ssid,
                         BK7258_WIFI_SCAN_SSID_LEN);
      required += IW_EV_LEN(ap_addr) + IW_EV_LEN(qual) + IW_EV_LEN(freq) +
                  IW_EV_LEN(data) + IW_EV_LEN(essid) +
                  ((ssid_len + 3u) & ~3u);
    }

  if (required > UINT16_MAX || iwr->u.data.pointer == NULL ||
      required > iwr->u.data.length)
    {
      iwr->u.data.length = required > UINT16_MAX ? UINT16_MAX : required;
      nxmutex_unlock(&priv->scan_lock);
      return -E2BIG;
    }

  {
    FAR uint8_t *cursor = iwr->u.data.pointer;

    for (index = 0; index < priv->scan_count; index++)
      {
        FAR struct iw_event *iwe = (FAR struct iw_event *)cursor;
        FAR struct bk7258_wifi_scan_ap_s *ap = &priv->scan_aps[index];
        iwe->cmd = SIOCGIWAP;
        iwe->u.ap_addr.sa_family = ARPHRD_ETHER;
        memcpy(iwe->u.ap_addr.sa_data, ap->bssid, sizeof(ap->bssid));
        iwe->len = IW_EV_LEN(ap_addr);
        cursor += iwe->len;

        ssid_len = strnlen(ap->ssid, BK7258_WIFI_SCAN_SSID_LEN);
        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = SIOCGIWESSID;
        iwe->u.essid.flags = 0;
        iwe->u.essid.length = ssid_len;
        iwe->u.essid.pointer = (FAR void *)sizeof(iwe->u.essid);
        memcpy(&iwe->u.essid + 1, ap->ssid, ssid_len);
        iwe->len = IW_EV_LEN(essid) + ((ssid_len + 3u) & ~3u);
        cursor += iwe->len;

        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = IWEVQUAL;
        iwe->u.qual.qual = 0;
        iwe->u.qual.level = (uint8_t)ap->rssi;
        iwe->u.qual.noise = 0;
        iwe->u.qual.updated = IW_QUAL_LEVEL_UPDATED | IW_QUAL_DBM |
                              IW_QUAL_QUAL_INVALID | IW_QUAL_NOISE_INVALID;
        iwe->len = IW_EV_LEN(qual);
        cursor += iwe->len;

        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = SIOCGIWFREQ;
        /* WEXT represents values 0..1000 as a channel number. This avoids
         * inventing a frequency conversion for a future/non-2.4GHz result. */
        iwe->u.freq.e = 0;
        iwe->u.freq.m = ap->channel;
        iwe->len = IW_EV_LEN(freq);
        cursor += iwe->len;

        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = SIOCGIWENCODE;
        iwe->u.data.flags = ap->security == WIFI_SECURITY_NONE ?
                            IW_ENCODE_DISABLED :
                            IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
        iwe->u.data.length = 0;
        iwe->u.data.pointer = NULL;
        iwe->len = IW_EV_LEN(data);
        cursor += iwe->len;
      }

    iwr->u.data.length = cursor - (FAR uint8_t *)iwr->u.data.pointer;
  }

  nxmutex_unlock(&priv->scan_lock);
  return OK;
#else
  (void)lower;
  (void)iwr;
  (void)set;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_range(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr)
{
  return -ENOSYS;
}

/****************************************************************************
 * Ops tables
 ****************************************************************************/

static const struct netdev_ops_s g_bk7258_net_ops =
{
  .ifup     = bk7258_wifi_ifup,
  .ifdown   = bk7258_wifi_ifdown,
  .transmit = bk7258_wifi_transmit,
  .receive  = bk7258_wifi_receive,
  .reclaim  = bk7258_wifi_reclaim,
};

static const struct wireless_ops_s g_bk7258_iw_ops =
{
  .connect    = bk7258_wifi_connect,
  .disconnect = bk7258_wifi_disconnect,
  .essid      = bk7258_wifi_essid,
  .bssid      = bk7258_wifi_bssid,
  .passwd     = bk7258_wifi_passwd,
  .mode       = bk7258_wifi_mode,
  .auth       = bk7258_wifi_auth,
  .country    = bk7258_wifi_country,
  .scan       = bk7258_wifi_scan,
  .range      = bk7258_wifi_range,
};

/****************************************************************************
 * Lifecycle / registration
 ****************************************************************************/

int bk7258_wifi_lower_init(struct bk7258_wifi_s *priv)
{
  memset(priv, 0, sizeof(*priv));

  priv->sta_mode = IW_MODE_INFRA;

  if (nxmutex_init(&priv->scan_lock) < 0)
    {
      return -ENOMEM;
    }

  priv->scan_lock_ready = true;

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  {
    bk_err_t ret = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                        bk7258_wifi_scan_done, priv);

    if (ret != BK_OK && ret != BK_ERR_EVENT_CB_EXIST)
      {
        nxmutex_destroy(&priv->scan_lock);
        priv->scan_lock_ready = false;
        return -EIO;
      }

    priv->scan_callback_registered = true;

    /* Association and disconnection drive the carrier flag; see the comment
     * above bk7258_wifi_sta_connected() for why carrier is dynamic here while
     * the authority keeps NETIF_FLAG_LINK_UP constant.
     *
     * Treated as fatal for the same reason the scan callback above is: if the
     * event module cannot take a callback, nothing about this driver works, and
     * a netdev whose carrier can never rise would accept an ifconfig and then
     * silently refuse to carry traffic. */

    ret = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_STA_CONNECTED,
                               bk7258_wifi_sta_connected, priv);
    if (ret == BK_OK || ret == BK_ERR_EVENT_CB_EXIST)
      {
        ret = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_STA_DISCONNECTED,
                                   bk7258_wifi_sta_disconnected, priv);
        if (ret != BK_OK && ret != BK_ERR_EVENT_CB_EXIST)
          {
            (void)bk_event_unregister_cb(EVENT_MOD_WIFI,
                                         EVENT_WIFI_STA_CONNECTED,
                                         bk7258_wifi_sta_connected);
          }
      }

    if (ret != BK_OK && ret != BK_ERR_EVENT_CB_EXIST)
      {
        (void)bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                     bk7258_wifi_scan_done);
        priv->scan_callback_registered = false;
        nxmutex_destroy(&priv->scan_lock);
        priv->scan_lock_ready = false;
        return -EIO;
      }

    priv->sta_event_registered = true;
  }
#endif

  return 0;
}

int bk7258_wifi_lower_uninit(struct bk7258_wifi_s *priv)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  if (priv->scan_callback_registered)
    {
      (void)bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                   bk7258_wifi_scan_done);
      priv->scan_callback_registered = false;
    }

  if (priv->sta_event_registered)
    {
      (void)bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_STA_CONNECTED,
                                   bk7258_wifi_sta_connected);
      (void)bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_STA_DISCONNECTED,
                                   bk7258_wifi_sta_disconnected);
      priv->sta_event_registered = false;
    }
#endif

  if (priv->registered)
    {
      netdev_lower_unregister(PRIV2LOWER(priv));
      priv->registered = false;
    }

  while (priv->rx_head != NULL)
    {
      FAR struct iob_s *iob = bk7258_wifi_receive(PRIV2LOWER(priv));
      if (iob != NULL)
        {
          iob_free_chain(iob);
        }
    }

  if (priv->scan_lock_ready)
    {
      nxmutex_destroy(&priv->scan_lock);
      priv->scan_lock_ready = false;
    }

  return 0;
}

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
static bk_err_t bk7258_wifi_scan_done(void *arg, event_module_t module,
                                      int event_id, void *event_data)
{
  FAR struct bk7258_wifi_s *priv = arg;
  FAR wifi_event_scan_done_t *done = event_data;
  wifi_scan_result_t result = {0};
  int ret;
  int ap_num;
  int index;
  struct bk7258_scan_diag_s diag;

  if (priv == NULL || module != EVENT_MOD_WIFI ||
      event_id != EVENT_WIFI_SCAN_DONE || done == NULL ||
      !priv->scan_lock_ready)
    {
      return BK_OK;
    }

  nxmutex_lock(&priv->scan_lock);
  if (!priv->scan_in_progress || done->scan_id != priv->scan_id)
    {
      nxmutex_unlock(&priv->scan_lock);
      return BK_OK;
    }
  nxmutex_unlock(&priv->scan_lock);

  bk7258_scan_diag_record(BK7258_SCAN_DIAG_COMPLETION_CALLBACK);

  /* The SDK allocates result.aps. Copy the bounded data below, then release
   * it unconditionally before this synchronous event callback returns. */

  ret = bk_wifi_scan_get_result(&result);
  if (ret != BK_OK)
    {
      bk7258_scan_diag_record(BK7258_SCAN_DIAG_RESULT_FETCH_FAIL);
    }

  nxmutex_lock(&priv->scan_lock);
  if (priv->scan_in_progress && done->scan_id == priv->scan_id)
    {
      priv->scan_count = 0;
      priv->scan_status = ret == BK_OK ? OK : -EIO;

      if (ret == BK_OK && result.ap_num > 0 && result.aps != NULL)
        {
          ap_num = result.ap_num;
          if (ap_num > BK7258_WIFI_SCAN_MAX_APS)
            {
              ap_num = BK7258_WIFI_SCAN_MAX_APS;
            }

          for (index = 0; index < ap_num; index++)
            {
              FAR struct bk7258_wifi_scan_ap_s *dst = &priv->scan_aps[index];
              FAR wifi_scan_ap_info_t *src = &result.aps[index];

              memset(dst, 0, sizeof(*dst));
              memcpy(dst->ssid, src->ssid, BK7258_WIFI_SCAN_SSID_LEN);
              dst->ssid[BK7258_WIFI_SCAN_SSID_LEN] = '\0';
              memcpy(dst->bssid, src->bssid, sizeof(dst->bssid));
              dst->rssi = src->rssi;
              dst->channel = src->channel;
              dst->security = (uint32_t)src->security;
            }

          priv->scan_count = ap_num;
        }

      priv->scan_in_progress = false;
      priv->scan_complete = true;
    }
  nxmutex_unlock(&priv->scan_lock);

  bk7258_scan_diag_result_exported(ret == BK_OK && result.ap_num > 0 ?
                                  (uint32_t)result.ap_num : 0);
  bk7258_scan_diag_snapshot(&diag);
  {
    /* EVERY MODEM-DOMAIN REGISTER READ IN THIS CALLBACK WAS REMOVED
     * (2026-09-08), for the same reason the parity banner's snapshot was:
     * NXMAC (0x49100000/0x49108000), the modem clock (0x49000000) and CRM
     * (0x49850000) all sit behind the MAC's clock gate, and with
     * CONFIG_PM_V2 && CONFIG_STA_PS compiled in the MAC is put into doze by
     * rwnxl_sleep() at the tail of rwnx_intf_init() (rw_task.c:1245) and is
     * re-woken only inside the core thread, by mac_wakeup_and_pwr_update()
     * (rw_task.c:821), for the duration of the work it dequeued.  A read of
     * a gated register stalls the AHB load forever: the CPU never retires
     * the instruction, so nothing faults (CONFIG_DEBUG_BUSFAULT reports bus
     * ERRORS, not stalls) and no other thread runs to print a clue.  The
     * banner version of this hang looked exactly like a silent console with
     * no nsh prompt and zero bytes of UART response.
     *
     * This callback runs on the event/app thread, NOT on the core thread, so
     * it has no wake guarantee at all -- the MAC may re-enter doze between
     * the scan completing and this callback being dispatched.  The removed
     * reads were written when rwnxl_sleep() was not compiled and the MAC
     * stayed awake for the whole session, which is why they used to work.
     *
     * The authority's own scan-done path reads no MAC register, so deleting
     * these converges on it rather than diverging.
     *
     * If MAC state has to be sampled again, sample it from a context that
     * owns a wake: inside the core thread while it is handling a BMSG (i.e.
     * after mac_wakeup_and_pwr_update() for that message), or from a
     * library callback that already runs with the MAC active.  Do NOT guard
     * such a read by reading a MAC register first -- that probe is itself
     * the stall.  The only safe status sources are the always-on SYS domain
     * (0x44010000, e.g. BK7258_SYS_POWER_WAKEUP below), libwifi's own RAM
     * (ke_state_get(), the ps_env flags) and AON PMU.
     *
     * Kept below: reads that cannot stall.  chan_env/txl_cntrl_env are SRAM
     * symbols resolved at link time; the OFDM/wakeup word is SYS-domain;
     * the ISR counters, the MM task state and the sd1..sd5 counters are all
     * plain memory. */
    extern uint8_t chan_env[];
    extern uint8_t txl_cntrl_env[];

    /* Console is 80 columns and overwrites past ~79 chars, so every
     * diagnostic keeps its payload under ~65 chars after the tag.  Three
     * consecutive boards lost fields to this (vcorehsel twice, the whole
     * MMSTART post line once), which is why these are split. */

    syslog(LOG_INFO, "[BK7258-WIFI] d2b chan=%08lx txhalt=%04x\n",
           (unsigned long)*(volatile uint32_t *)(chan_env + 0x28),
           (unsigned int)*(volatile uint16_t *)(txl_cntrl_env + 0x16e));

    /* SYS domain only (0x44010000 is never gated): the OFDM power-down vote
     * the library reads back through crm_mdm_reset, and the raw wakeup
     * word it lives in.  The `crm14=` field that used to close this line
     * read 0x49850014 and went with the rest. */

    syslog(LOG_INFO, "[BK7258-WIFI] d3a ofdm=%lu pwak=%08lx\n",
           (unsigned long)bk7258_wifi_pwd_ofdm_get_override(),
           (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP));
  }
  {
    extern volatile uint32_t bk7258_wifi_isr_count[64];
    extern int ke_state_get(uint8_t task_id);

    uint32_t state = (unsigned)ke_state_get(0);  /* TASK_MM = 0 */

    /* THREE PROBES DELETED HERE (2026-09-01) -- all three dereferenced
     * hard-coded closed-library RAM addresses that have since DRIFTED:
     *
     *   cm      0x280700c0 -> now lands in socket_entity+4 (a 12-byte symbol)
     *   in_doze 0x2807bdb1 -> now lands in scanu_env+733
     *   lowpll  0x2807bdb8 -> now lands in scanu_env+740
     *
     * Proof of drift, measured in THIS session: chan_env / txl_cntrl_env /
     * rwnx_env / ke_env each moved by exactly +0x28 between two builds,
     * because adding scan_ssid[33]+scan_ssid_len to struct bk7258_wifi_s
     * pushed every following BSS symbol along.  Any literal address into
     * library RAM is invalidated by an unrelated struct change.
     *
     * Consequence that must be carried forward: the repeated
     * "in_doze=0 lowpll=0, therefore the MAC is not sleeping" conclusion is
     * WITHDRAWN for our side -- those bytes were never the doze status.  The
     * authority board's own in_doze probe is unaffected (its build, its
     * addresses).
     *
     * Rule: reach library state through a linkable global (extern) or a
     * function call, never through a literal.  chan_env/txl_cntrl_env below
     * are `extern` and therefore resolved at link time; ke_state_get is a
     * call.  Those stay. */

    /* Per-source interrupt counts.  The library registers seven handlers via
     * _bk_int_isr_register (board log: sources 36,35,34,33,31,30,29) and the
     * counters have been collected all along in hw_driver_shim.c -- only
     * index 36 was ever printed.  For the stalled directed scan the decisive
     * one is 34 (MAC TX trigger): the probe frame reaches txl_frame_push yet
     * `frame released without call cb` says its confirm never ran, so either
     * the completion interrupt never arrives (count 0 -> enable/routing, or
     * the MAC never raised it) or it arrives and the dispatch chain drops it
     * (count > 0 -> the HISR/workqueue semantics).
     * Source map (Armino sys_struct.h:463-465 + high-bank bits 0-4):
     *   29 modem_mpb  30 modem_riu  31 mac_txrx_timer
     *   32 txrx_misc  33 rx_trigger 34 tx_trigger  35 prot  36 gen */

    syslog(LOG_INFO, "[BK7258-WIFI] isr 29=%lu 30=%lu 31=%lu 32=%lu\n",
           (unsigned long)bk7258_wifi_isr_count[29],
           (unsigned long)bk7258_wifi_isr_count[30],
           (unsigned long)bk7258_wifi_isr_count[31],
           (unsigned long)bk7258_wifi_isr_count[32]);
    syslog(LOG_INFO, "[BK7258-WIFI] isr 33=%lu 34=%lu 35=%lu 36=%lu\n",
           (unsigned long)bk7258_wifi_isr_count[33],
           (unsigned long)bk7258_wifi_isr_count[34],
           (unsigned long)bk7258_wifi_isr_count[35],
           (unsigned long)bk7258_wifi_isr_count[36]);
    syslog(LOG_INFO, "[BK7258-WIFI] mmstate=%lu\n", (unsigned long)state);
    /* WPROBE3 REMOVED (2026-09-01).  It hard-coded 0x2806fb90 as "the hal env
     * pointer" and dereferenced +0xe4/e8/ec/f8 as setfreq inputs.  nm proves
     * that word holds the closed library's saved FUNCS-TABLE pointer
     * (0x2806f810 = g_wifi_os_funcs), so all four "inputs" resolved +0 to our
     * own slot functions (hp_uap_ip_start, hp_uap_ip_down,
     * hp_net_wlan_add_netif, bk7258_wifi_set_sta_status_cb).  Every reading it
     * produced was garbage.
     *
     * The wider lesson (user directive): hard-referencing closed-library
     * internal RAM from outside is not how this port gets fixed.  Eight such
     * probe-driven candidates were falsified this session, while all six real
     * defects came from diffing OUR sources against Armino's sources.
     * Hardware registers stay fair game -- their semantics are recoverable
     * from Armino's own accessor functions -- but library-internal addresses
     * are not.  Remaining offenders of this kind, kept only because they at
     * least report what they claim: the 0x2807bdb1/0x2807bdb8 doze bytes
     * above and the chan_env/txl_cntrl_env externs in scan diag2. */
    /* The "M1: 49108050" line was REMOVED with the rest of the modem-domain
     * reads (2026-09-08).  0x49108050 is in the MAC interrupt block and is
     * gated with the MAC, so reading it here stalls exactly like the others.
     * What it reported -- that hal_machw_init() reached its interrupt-enable
     * step -- is already implied by the scan producing results at all, and
     * by the isr counters printed above (a nonzero count for source 33/34/36
     * cannot happen unless the MAC's interrupt enable was programmed). */
  }
  /* The [NXWIN] register-window dump was REMOVED (2026-09-08): 44 words of
   * getreg32(0x49100000 + n) is the single largest concentration of gated
   * NXMAC reads in the tree, and the first iteration is enough to wedge the
   * CPU on a doze'd MAC.
   *
   * It was a parity tool, not a runtime diagnostic -- its whole purpose was
   * to be diffed against the same dump taken on the authority board.  If
   * that comparison is needed again, take it from a context that holds a
   * wake (see the note at the head of this callback), or better, take it on
   * both boards with STA_PS off in a throwaway image whose only job is the
   * dump; do not carry it in the normal scan path.
   */
  /* The `d3f` probe that used to sit here is DELETED (2026-09-01).
   *
   * It sampled 0x49100010 twice across a 200 us delay and printed it as
   * `tick=`, on the belief that the register was a free-running ke_timer /
   * mm_timer counter.  It is not: 0x49100010 / 0x14 are the NXMAC MAC-ADDRESS
   * registers (authority accessors nxmac_mac_addr_low_get / _hi_get read
   * [base+0x10] / [base+0x14]), and the board proves it -- once bk_get_mac was
   * fixed the pair went from 0/0 to 468c47c8 / 00001502, i.e. exactly the
   * c8:47:8c:46:02:15 read out of flash, matching the authority byte for byte.
   * So every earlier "the tick counter is dead" reading was really "the MAC
   * address register is zero", and the 200 us delay measured nothing.
   *
   * NXWIN row 0x10 already prints both words, so nothing is lost by removing
   * this.  0x491000ac was printed alongside it as `ac=` on an equally
   * unverified guess (nxmac_lp_clk_32786_hz_setf) and is dropped with it. */

  /* MM_RESET/MM_START hardware latches, re-reported from this quiet context.
   * Printing at the sample sites raced with other threads' console output: on
   * sta-mmstate-20260901 the `post` line vanished and the `pre` line was cut
   * in half, losing the only measurement that image existed to take.
   *
   * DO NOT read ke=0 / r38=0 at the post-start column as "mm_active() never
   * ran".  A three-way table saying that was written here once and is wrong:
   * mm_start_req_handler sends the CFM *before* it calls mm_active() and
   * ke_state_set(), then parks the MAC via hal_machw_idle_req().  So the
   * instant rw_msg_send() returns is either ahead of mm_active() or already
   * past the park, and ke=0 / r38=0 there is the expected success path.
   * Board evidence (sta-arminoalign-20260902): all four columns read ke=0,
   * r38=0, ret=0, while the end-of-scan probe reports mmstate=MM_ACTIVE(1)
   * and r38=0x33 -- MM did start.  These four columns therefore say nothing
   * about MM bring-up; they are only useful for registers that are expected
   * to be stable across the whole handshake.
   * States: 0=MM_IDLE 1=MM_ACTIVE 2=GOING_TO_IDLE 3=HOST_BYPASSED
   *         4=MM_NO_IDLE (lmac_msg.h:885-899).  ke=-1 marks a site that has
   *         no ke_state to report (the two reset points).
   *
   * This replaces the old mm1/mm2 pair: hp0 carries the same seen/state/ret
   * fields and hp1 the same r38 values, over four sample points instead of
   * two, so keeping both would only spend scarce console lines twice. */

  bk7258_hwprobe_report();
  /* Was a single 294-character line -- the worst truncation offender in the
   * whole diagnostic set; on an 80-column console only its first ~79 chars
   * plus the final character ever arrived, so 12 of these 17 counters were
   * never actually readable.  Four short lines instead. */

  syslog(LOG_INFO, "[BK7258-WIFI] sd1 req=%lu act=%lu pas=%lu\n",
         (unsigned long)diag.requested_channels,
         (unsigned long)diag.active_channels,
         (unsigned long)diag.passive_channels);
  syslog(LOG_INFO, "[BK7258-WIFI] sd2 ind=%lu done=%lu ins=%lu\n",
         (unsigned long)diag.lmac_result_ind,
         (unsigned long)diag.lmac_complete,
         (unsigned long)diag.result_inserted);
  syslog(LOG_INFO, "[BK7258-WIFI] sd3 full=%lu cty=%lu dup=%lu oom=%lu\n",
         (unsigned long)diag.result_table_full,
         (unsigned long)diag.result_country_drop,
         (unsigned long)diag.result_duplicate,
         (unsigned long)diag.result_alloc_fail);
  syslog(LOG_INFO, "[BK7258-WIFI] sd4 bcn=%lu pr=%lu nosta=%lu\n",
         (unsigned long)diag.host_mgmt_beacon,
         (unsigned long)diag.host_mgmt_probe_resp,
         (unsigned long)diag.host_mgmt_no_sta_vif);
  syslog(LOG_INFO, "[BK7258-WIFI] sd5 qdrop=%lu fwd=%lu exp=%lu fail=%lu\n",
         (unsigned long)diag.host_mgmt_wpaq_drop,
         (unsigned long)diag.host_mgmt_wpaq_forwarded,
         (unsigned long)diag.result_exported,
         (unsigned long)diag.result_fetch_fail);

  bk_wifi_scan_free_result(&result);
  return BK_OK;
}

/* Declared here rather than by including <bk_private/bk_rw.h>: that header
 * pulls in fhost_msg.h, fhost_mac.h and pbuf.h, and our own
 * hal_port/include/pbuf.h includes bk_rw.h back, so including it here would drag a
 * circular chain into this file.  Same single-symbol extern pattern the
 * chan_env/txl_cntrl_env declarations in the scan-done probe already use.
 *
 * Provenance: bk_rw.h:389, defined at rwnx_utils.c:108 (which forwards to
 * mac_vif_mgmt_mac_to_index).  Both are linked into the image -- verified with
 * nm: rwm_mgmt_vif_mac2idx and mac_vif_mgmt_mac_to_index are both T.
 *
 * wifi_netif_mac_to_vifid() would have been the tidier wrapper, but
 * wifi_netif.c is not in the build set (wifi/CMakeLists.txt lists only
 * rwnx_utils.c from that directory), so the symbol is absent from the ELF. */

extern uint8_t rwm_mgmt_vif_mac2idx(void *mac);

/* Association is the point where the link becomes usable, so it is where
 * carrier rises.
 *
 * Ordering is what makes this correct rather than merely plausible.  The
 * authority posts EVENT_WIFI_STA_CONNECTED from notify.c:346-370, *after* the
 * key material has been installed into the MAC.  Board evidence
 * (evidence-20260908/bk7258-connect-SUCCESS-2-26a68589.log) shows exactly
 * that order:
 *
 *   1962  [hitf] add hw key idx=8      <- PTK into a hardware slot
 *   1988  [hitf] add hw key idx=1      <- GTK into a hardware slot
 *   1989  [wpa]  Key negotiation completed [PTK=CCMP GTK=CCMP]
 *   1998  [wpa]  CTRL-EVENT-CONNECTED
 *
 * So by the time carrier rises the encrypted data path is genuinely usable;
 * there is no window where the stack could transmit before the keys are in
 * place.
 *
 * This differs in shape from the authority, which never raises link state
 * dynamically at all: it burns NETIF_FLAG_LINK_UP into the flags initialiser
 * (wlanif.c:137) because its lwIP netif is created on association, so the
 * netif's existence *is* the link-up signal.  NuttX registers the netdev once
 * at boot (bk7258_wifi_lower_register below, from the init path), long before
 * any association, so the same fact has to be carried by the carrier flag
 * instead.  Adapting this is an RTOS integration point, not a divergence --
 * unconditionally raising carrier at register time would be the divergence,
 * because the stack would then transmit while unassociated.
 */

static bk_err_t bk7258_wifi_sta_connected(void *arg, event_module_t module,
                                          int event_id, void *event_data)
{
  FAR struct bk7258_wifi_s *priv = arg;
  uint8_t vif_idx;

  if (priv == NULL || module != EVENT_MOD_WIFI ||
      event_id != EVENT_WIFI_STA_CONNECTED)
    {
      return BK_OK;
    }

  /* event_data is deliberately unused: wifi_event_sta_connected_t carries only
   * ssid and bssid (wifi_types.h:684-687), no vif index, so the vif has to be
   * resolved from our own MAC below. */

  (void)event_data;

  if (!priv->registered)
    {
      /* The event callback is installed in bk7258_wifi_lower_init(), which
       * runs before bk7258_wifi_lower_register().  Nothing can associate in
       * that window, but touching an unregistered netdev is not worth the
       * risk of being wrong about that. */

      syslog(LOG_WARNING, "[BK7258-WIFI] connected: netdev not registered\n");
      return BK_OK;
    }

  /* Resolve the STA vif index the same way the authority does when it needs
   * the vif for its own MAC: rwm_mgmt_vif_mac2idx() on the station address
   * (wifi_v2.c:3086 passes &g_sta_param_ptr->own_mac to it).  The zero-MAC
   * rejection is the authority's too, from the wrapper we cannot link
   * (wifi_netif.c:31-33) -- a zeroed address must not be looked up, because
   * index 0 is a valid vif and would silently look like success.
   *
   * Until now vif_idx was never assigned anywhere, so it held the zero left by
   * the static initialiser.  Zero happens to be the right STA vif, which is
   * why TX worked, but by coincidence rather than by construction.  Filling it
   * from the association matters because bmsg_tx_handler() returns *without
   * freeing* when vif_idx == INVALID_VIF_IDX (rw_task.c:178), a failure that
   * from the outside is indistinguishable from "the frame was never
   * submitted". */

  if ((priv->mac[0] | priv->mac[1] | priv->mac[2] |
       priv->mac[3] | priv->mac[4] | priv->mac[5]) == 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connected: own MAC is zero; "
                      "keeping vif=%u\n", (unsigned)priv->vif_idx);
    }
  else
    {
      vif_idx = rwm_mgmt_vif_mac2idx(priv->mac);
      if (vif_idx == BK7258_INVALID_VIF_IDX)
        {
          syslog(LOG_ERR, "[BK7258-WIFI] connected: vif lookup failed; "
                          "keeping vif=%u\n", (unsigned)priv->vif_idx);
        }
      else
        {
          priv->vif_idx = vif_idx;
        }
    }

  syslog(LOG_INFO, "[BK7258-WIFI] connected: carrier on, vif=%u\n",
         (unsigned)priv->vif_idx);

  bk7258_wifi_lower_carrier_on(priv);
  return BK_OK;
}

static bk_err_t bk7258_wifi_sta_disconnected(void *arg, event_module_t module,
                                             int event_id, void *event_data)
{
  FAR struct bk7258_wifi_s *priv = arg;
  FAR wifi_event_sta_disconnected_t *info = event_data;

  if (priv == NULL || module != EVENT_MOD_WIFI ||
      event_id != EVENT_WIFI_STA_DISCONNECTED)
    {
      return BK_OK;
    }

  if (!priv->registered)
    {
      return BK_OK;
    }

  /* Carrier drops on every disconnect, including a locally requested one:
   * either way the encrypted data path is gone.  netdev_carrier_off() is
   * guarded by IFF_IS_RUNNING (netdev_carrier.c:84), so a disconnect event
   * arriving while carrier is already down is a no-op -- which matters,
   * because EVENT_WIFI_STA_DISCONNECTED is posted from several places
   * (rw_msg_rx.c:1630, notify.c:471,488, config_none.c:168,231,
   * wpa_scan.c:2912) and may well arrive more than once per failure. */

  syslog(LOG_INFO, "[BK7258-WIFI] disconnected: carrier off, reason=%d "
                   "local=%d\n",
         info != NULL ? info->disconnect_reason : -1,
         info != NULL ? (int)info->local_generated : -1);

  bk7258_wifi_lower_carrier_off(priv);
  return BK_OK;
}
#endif

int bk7258_wifi_lower_register(struct bk7258_wifi_s *priv)
{
  int ret;

  priv->lower.ops = &g_bk7258_net_ops;
#ifdef CONFIG_NETDEV_WIRELESS_HANDLER
  priv->lower.iw_ops = &g_bk7258_iw_ops;
#endif
  atomic_init(&priv->lower.quota[NETPKT_TX], 1);

  /* RX quota 4: quota 1 silently drops the ARP reply when competing
   * broadcast/multicast frames (mDNS, SSDP, neighbor ARP) arrive in the
   * same window.  netpkt_alloc() returns NULL on quota exhaustion and the
   * frame is freed in rx_submit before netdev statistics, so ifconfig
   * shows Dropped=0 while frames are lost.  Board evidence: 3 bmsg_rx_handler
   * calls after the ARP request with quota=1, yet arp_wait timed out.
   *
   * The earlier quota=8 attempt coincided with a scan hang, but the probe
   * proving the changed line executed was itself upstream of it (the rx#
   * probe printed nothing), so the correlation is unexplained and likely
   * coincidental.  quota=4 is a conservative middle ground. */

  atomic_init(&priv->lower.quota[NETPKT_RX], 4);

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  /* Link-layer address.  This is the counterpart of the authority's
   * low_level_init(), which fills the lwIP netif from the vif's MAC:
   *
   *   netif->hwaddr_len = ETHARP_HWADDR_LEN;
   *   os_memcpy(netif->hwaddr, macptr, ETHARP_HWADDR_LEN);
   *
   * (cp/components/lwip_intf_v2_1/lwip-2.1.2/port/wlanif.c:130-131, where
   * macptr is wifi_netif_vif_to_mac(vif)).  Same fact, different field name:
   * NuttX keeps the link-layer address in net_driver_s.d_mac, lwIP in
   * netif->hwaddr.
   *
   * Done here rather than on association because the address is a property of
   * the chip, not of the link: bk_wifi_init() has already run by the time this
   * is reached, so the accessor resolves to the cached bk_get_mac() value.
   *
   * A failure is not fatal.  Leaving d_mac zeroed keeps the interface
   * registered and inspectable via ifconfig, which is what makes the failure
   * diagnosable at all -- whereas refusing to register would remove the only
   * place the missing address is visible. */

  if (bk7258_wifi_sta_own_mac(priv->mac) == OK)
    {
      memcpy(PRIV2LOWER(priv)->netdev.d_mac.ether.ether_addr_octet,
             priv->mac, sizeof(priv->mac));
    }
  else
    {
      syslog(LOG_ERR, "[BK7258-WIFI] register: own MAC unavailable; "
                      "d_mac left zeroed\n");
    }
#endif

  ret = netdev_lower_register(PRIV2LOWER(priv), NET_LL_IEEE80211);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;
  priv->carrier = false;
  return OK;
}

void bk7258_wifi_lower_carrier_on(struct bk7258_wifi_s *priv)
{
  priv->carrier = true;
  netdev_lower_carrier_on(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_carrier_off(struct bk7258_wifi_s *priv)
{
  priv->carrier = false;
  netdev_lower_carrier_off(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_rx_ready(struct bk7258_wifi_s *priv)
{
  netdev_lower_rxready(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_tx_done(struct bk7258_wifi_s *priv)
{
  netdev_lower_txdone(PRIV2LOWER(priv));
}

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME

/* Both of the following are runtime-gated for the same reason the EAPOL split
 * in ethernetif_input() is: bk_wifi_sta_get_mac() comes from <modules/wifi.h>,
 * and BK7258_ETH_HDR_LEN and <syslog.h> are equally only in scope under this
 * symbol (see the include block at the top of the file).  The CP image builds
 * with the vendor runtime disabled and must not reference any of them. */

int bk7258_wifi_sta_own_mac(FAR uint8_t *mac)
{
  if (mac == NULL)
    {
      return -EINVAL;
    }

  /* The same accessor the EAPOL RX split uses above, deliberately: it is what
   * the vendor bridge treats as the vif's address, so a frame built with it
   * carries the source the closed library expects.  bk7258_wifi_board_get_mac()
   * is NOT equivalent -- it is a raw partition read, and bk_get_mac() applies
   * validation and per-type handling on top of it (hal_port_mac.c:100,126). */

  if (bk_wifi_sta_get_mac(mac) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

int bk7258_wifi_lower_tx_inject(FAR const uint8_t *frame, uint16_t len)
{
  FAR struct bk7258_wifi_s *priv = &g_bk7258_wifi;
  struct pbuf *p;

  if (frame == NULL)
    {
      return -EINVAL;
    }

  /* Strictly greater than the header: the vendor descriptor describes the
   * payload as (p->payload + sizeof(ETH_HDR_T), len - sizeof(ETH_HDR_T))
   * (rwnx_tx.c:753), so a 14-byte frame would claim a zero-length payload. */

  if (len <= BK7258_ETH_HDR_LEN || len > BK7258_WIFI_FRAME_MAX)
    {
      return -EMSGSIZE;
    }

  if (!priv->registered)
    {
      return -ENODEV;
    }

  p = pbuf_alloc(PBUF_RAW_TX, len, PBUF_RAM);
  if (p == NULL)
    {
      return -ENOMEM;
    }

  memcpy(p->payload, frame, len);

  /* priv->vif_idx is never assigned anywhere in the tree, so it is the zero
   * from static initialisation.  Zero happens to be the right STA vif -- the
   * reference TX descriptors log vif=0 -- but by coincidence, not by design;
   * it has to be filled from the association event before the data plane is
   * integrated.  Printed here because bmsg_tx_handler() returns without
   * freeing when vif_idx == INVALID_VIF_IDX (rw_task.c:178), which from the
   * outside looks identical to "the frame was never submitted". */

  syslog(LOG_WARNING, "[BK7258] txinject len=%u vif=%u\n",
         (unsigned)len, (unsigned)priv->vif_idx);

  return bk7258_wifi_tx_pbuf(priv, p);
}

#endif /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

void bk7258_wifi_lower_rx_submit(struct bk7258_wifi_s *priv,
                                 struct bk7258_vpkt *vpkt)
{
  FAR struct iob_s *iob;
  unsigned int len;

  if (priv == NULL || vpkt == NULL || !priv->registered)
    {
      bk7258_vpkt_free(vpkt);
      return;
    }

  /* The lower bound is now a hard requirement, not just a sanity check: the
   * netpkt_copyin() below writes the frame starting at -NET_LL_HDRLEN, so a
   * frame without a full Ethernet header plus at least one payload byte would
   * reach the stack with io_pktlen == 0. */

  len = vpkt->tot_len;
  if (len <= BK7258_ETH_HDR_LEN || len > BK7258_WIFI_FRAME_MAX)
    {
      bk7258_vpkt_free(vpkt);
      return;
    }

  /* netpkt_alloc/netpkt_copyin rather than raw iob calls, because the offset
   * arithmetic is the whole bug.  netpkt_copyin() passes
   * `offset - NET_LL_HDRLEN` down to iob_trycopyin(), so offset 0 writes at -14
   * and the Ethernet header lands in the reserve area, leaving IOB_DATA on the
   * L3 payload.  That is what eth_input() and arp_in() expect:
   *
   *   IPBUF(hl) = IOB_DATA(d_iob) + hl          netdev.h:206
   *   NETLLBUF  = IPBUF(0) - NET_LL_HDRLEN      netdev.h:207
   *
   * The previous code did iob_reserve() + iob_trycopyin(offset 0) by hand, which
   * put the whole frame after the reserve area and left IOB_DATA on the Ethernet
   * header -- one header too early for every stack reader.  Board evidence:
   * well-formed ARP replies arrived (rx#13 et=0806 tail=00 01 08 00 06 04) while
   * arp_in() reported "Invalid hardware type", because ah_hwtype was read from
   * the destination MAC.
   *
   * netpkt_alloc() also accounts the RX quota, which the raw iob_tryalloc() path
   * never did while the upper half kept returning quota in netpkt_put(). */

  iob = netpkt_alloc(PRIV2LOWER(priv), NETPKT_RX);
  if (iob == NULL)
    {
      bk7258_vpkt_free(vpkt);
      return;
    }

  if (netpkt_copyin(PRIV2LOWER(priv), iob, vpkt->payload, len, 0) < 0)
    {
      netpkt_free(PRIV2LOWER(priv), iob, NETPKT_RX);
      bk7258_vpkt_free(vpkt);
      return;
    }

  /* No netpkt_setdatalen() here.  The spec lists a missing setdatalen as an
   * anti-pattern (eth_netdev_pattern.md:766-773), but that case is a driver
   * whose copy does not update the packet length.  netpkt_copyin() ->
   * iob_trycopyin() already extends io_pktlen over what it wrote, and the board
   * agrees: the run without setdatalen reported RX Bytes=0x2a for a 42-byte ARP
   * frame, i.e. the length was already right.  Adding the call on top coincided
   * with scan hanging, so it stays out. */

  if (bk7258_wifi_rx_enqueue(priv, iob) < 0)
    {
      netpkt_free(PRIV2LOWER(priv), iob, NETPKT_RX);
      bk7258_vpkt_free(vpkt);
      return;
    }

  bk7258_vpkt_free(vpkt);
  netdev_lower_rxready(PRIV2LOWER(priv));
}

/****************************************************************************
 * Public entry
 ****************************************************************************/

/* One-line-per-subsystem parity snapshot printed once at init completion.
 * Reads only: no SSID/BSSID/frame data.  The slot list covers every group
 * where the authoritative initializer binds and this port may stay NULL
 * (EVM/ATE, netif/IP glue, power-save stubs, CSI, vendor-IE, airkiss,
 * low-analog, dcache) so a bring-up trace shows the remaining divergence
 * surface without another audit round. */
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
static void bk7258_wifi_parity_banner(void)
{
  static const struct
  {
    const char *name;
    size_t off;
  } slots[] =
  {
    { "_do_evm",                     offsetof(wifi_os_funcs_t, _do_evm) },
    { "_bk_feature_csi_out_cb",      offsetof(wifi_os_funcs_t, _bk_feature_csi_out_cb) },
    { "_send_udp_bc_pkt",            offsetof(wifi_os_funcs_t, _send_udp_bc_pkt) },
    { "_save_net_info",              offsetof(wifi_os_funcs_t, _save_net_info) },
    { "_sta_ip_down",                offsetof(wifi_os_funcs_t, _sta_ip_down) },
    { "_mac_sleeped",                offsetof(wifi_os_funcs_t, _mac_sleeped) },
    { "_mcu_ps_machw_init",          offsetof(wifi_os_funcs_t, _mcu_ps_machw_init) },
    { "_sys_hal_enter_low_analog",   offsetof(wifi_os_funcs_t, _sys_hal_enter_low_analog) },
    { "_flush_all_dcache",           offsetof(wifi_os_funcs_t, _flush_all_dcache) },
    { "_vendor_ie_cb",               offsetof(wifi_os_funcs_t, _bk_wifi_get_vendor_ie_cb_internal) },
  };
  char nulls[144];
  size_t used = 0;

  nulls[0] = '\0';
  for (size_t i = 0; i < nitems(slots); i++)
    {
      if (*(void *const *)((const char *)&g_wifi_os_funcs + slots[i].off) == NULL)
        {
          int written = snprintf(nulls + used, sizeof(nulls) - used,
                                 "%s ", slots[i].name);
          if (written < 0 || (size_t)written >= sizeof(nulls) - used)
            {
              break;
            }
          used += (size_t)written;
        }
    }

  syslog(LOG_INFO,
         "[BK7258-WIFI] parity: null-vs-authoritative: %s\n",
         nulls[0] != '\0' ? nulls : "(none)");

  /* The NXMAC/CRM register snapshot that used to follow this line was
   * removed (2026-09-08).  It read 0x49100120 twice, then 0x49100000,
   * 0x49100504, 0x49100038 and 0x49850010, which is no longer a legal
   * access point.
   *
   * With CONFIG_PM_V2 && CONFIG_STA_PS compiled in, rwnx_intf_init() ends in
   * rwnxl_sleep() (rw_task.c:1245), so the MAC is deliberately in doze by the
   * time this banner runs.  Reading a clock-gated NXMAC stalls the AHB load
   * indefinitely: the CPU never retires the instruction, so no fault is
   * raised (CONFIG_DEBUG_BUSFAULT cannot report a stall, only an error) and
   * no other thread ever runs to print anything.
   *
   * That is precisely the hang this banner caused.  The [initseq] boundary
   * trace showed every init step completing normally -- including
   * rwnxl_sleep() itself and bk_wifi_init() returning with
   * "wifi inited(1) ret(0)" -- and then the syslog above was the last output
   * on the console, with no nsh prompt for the rest of the capture.
   *
   * The snapshot had also stopped measuring what it was written for.  It
   * dates from the scan era, when rwnxl_sleep() was not compiled and the MAC
   * stayed awake, so an active FSM value here carried information.  After the
   * sleep call it can only ever show doze.  Sample the MAC while it is known
   * to be awake (the scan and assoc paths already do) rather than
   * reinstating these reads at init completion.
   */
}
#endif

bool bk7258_wifi_is_ready(void)
{
  return atomic_load_explicit(&g_bk7258_wifi_init_state,
                              memory_order_acquire) ==
         BK7258_WIFI_INIT_READY;
}

/****************************************************************************
 * STA association
 *
 * Transcribed from the authority's own caller, demo_sta_app_init()
 * (cp/components/bk_wifi/src/wifi_api.c:185-210), which is the variant that
 * takes just an SSID and a passphrase -- no BSSID, no hard-coded channel.
 * That file also sits in our tree byte-identical to the authority copy (it is
 * simply not compiled), so the sequence below can be checked against it line
 * by line.  The two callees are byte-identical to the authority as well: our
 * wifi_v2.c differs from cp/components/bk_wifi/src/wifi_v2.c in only four
 * places (include depth, one #if/#ifdef, the g_wifi_funcs/g_wifi_vars
 * definition that belongs to funcs_fill.c here, and a trailing newline) --
 * none of them inside a function body.
 *
 * Guarded like bk7258_wifi_scan(): bk_wifi_sta_start() reaches
 * wpa_psk_request() and wlan_sta_enable(), whose providers
 * (wpa_psk_cache.c, wpa_ctrl_iface.c, sa_station.c) are inside the
 * CONFIG_BK7258_WIFI_VENDOR_RUNTIME block of CMakeLists.txt, so the
 * runtime-disabled CP image must not reference them.
 ****************************************************************************/

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME

/* Both vendor buffers are NUL-terminated (wifi_types.h:63 and :67):
 * WIFI_SSID_STR_LEN == 32+1, WIFI_PASSWORD_LEN == 64+1.  A WPA2 passphrase is
 * 8..63 characters, or exactly 64 hex digits when a raw PMK is supplied. */

int bk7258_wifi_sta_connect(FAR const char *ssid, FAR const char *psk)
{
  wifi_sta_config_t config;
  size_t ssid_len;
  size_t psk_len;
  bk_err_t ret;

  if (ssid == NULL || psk == NULL)
    {
      return -EINVAL;
    }

  /* The vendor call chain asserts on an uninitialized stack: bk_wifi_init()
   * must have run so that cfg_param_init() has allocated g_sta_param_ptr,
   * which bk_wifi_sta_set_config() dereferences without a NULL check. */

  if (!bk7258_wifi_is_ready())
    {
      return -ENODEV;
    }

  ssid_len = strlen(ssid);
  psk_len = strlen(psk);

  /* DELIBERATE DEVIATION from the transcribed original, which uses
   * os_strcpy() for both fields and length-checks only the SSID
   * (wifi_api.c:191-204).  That is safe there because its arguments are
   * compile-time constants; ours arrive from a command line, so an
   * over-long argument would run off a 33- or 65-byte struct member.  The
   * copies below are bounded and both lengths are rejected up front. */

  if (ssid_len == 0u || ssid_len >= sizeof(config.ssid))
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: ssid length %u, need 1..%u\n",
             (unsigned)ssid_len, (unsigned)(sizeof(config.ssid) - 1u));
      return -EINVAL;
    }

  /* psk_len == 0 selects an open (no-password) network: config.password
   * stays zeroed after the memset below, and the vendor supplicant
   * auto-detects security from scan results.  A non-empty PSK must still
   * satisfy the 8..63 / 64-hex rules. */
  if (psk_len > 0u &&
      (psk_len < BK7258_WIFI_PSK_MIN_LEN ||
       psk_len >= sizeof(config.password)))
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: psk length %u, need %u..%u "
             "or 0 for open\n",
             (unsigned)psk_len, (unsigned)BK7258_WIFI_PSK_MIN_LEN,
             (unsigned)(sizeof(config.password) - 1u));
      return -EINVAL;
    }

  /* Zeroed exactly as the original does with `= {0}`.  Two consequences are
   * load-bearing rather than incidental:
   *   - reserved[32] must be all zero or wifi_sta_validate_config() rejects
   *     the config outright (wifi_v2.c:2669, WIFI_RESERVED_BYTE_VALUE == 0);
   *   - is_user_fast_connect must stay 0, otherwise validate_config takes the
   *     g_fci overwrite branch at wifi_v2.c:2672.
   * security is left 0 (== WIFI_SECURITY_NONE) because the original leaves it
   * so; the STA path never reads it -- wifi_sta_set_global_config()
   * (wifi_v2.c:2732) copies ssid and password but not security.
   */

  memset(&config, 0, sizeof(config));
  memcpy(config.ssid, ssid, ssid_len);
  memcpy(config.password, psk, psk_len);

  /* SSID and lengths only.  The passphrase itself is never logged, here or
   * in the status path. */

  if (psk_len == 0u)
    {
      syslog(LOG_INFO, "[BK7258-WIFI] connect: ssid=\"%s\" (%u), open\n",
             config.ssid, (unsigned)ssid_len);
    }
  else
    {
      syslog(LOG_INFO, "[BK7258-WIFI] connect: ssid=\"%s\" (%u), psk %u chars\n",
             config.ssid, (unsigned)ssid_len, (unsigned)psk_len);
    }

  /* set_config before start, as in the original.  set_config is also what
   * populates g_sta_param_ptr->ssid/key, which is what wpa_psk_request()
   * reads inside bk_wifi_sta_start() (wifi_v2.c:2477); calling start alone
   * would derive a PSK from an empty SSID and key.  If a link is already up,
   * set_config disconnects first and re-connects on its own
   * (wifi_v2.c:2983-3010). */

  ret = bk_wifi_sta_set_config(&config);
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: set_config failed=%d\n",
             (int)ret);
      return -EIO;
    }

  ret = bk_wifi_sta_start();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: sta_start failed=%d\n",
             (int)ret);
      return -EIO;
    }

  /* Association and the 4-way handshake run asynchronously from here; the
   * caller polls bk7258_wifi_sta_connect_status(). */

  syslog(LOG_INFO, "[BK7258-WIFI] connect: request accepted\n");
  return OK;
}

int bk7258_wifi_sta_connect_status(FAR int *state, FAR int *reason)
{
  wifi_linkstate_reason_t info;
  bk_err_t ret;

  if (!bk7258_wifi_is_ready())
    {
      return -ENODEV;
    }

  /* bk_wifi_sta_get_linkstate_with_reason(), not bk_wifi_sta_get_link_status():
   * the latter returns early with a bare DISCONNECTED once
   * wifi_sta_is_connected() is false (wifi_v2.c:3078-3082), which discards the
   * reason code precisely when a failure needs explaining.  This one reads
   * mhdr_get_station_status() directly and keeps both fields. */

  memset(&info, 0, sizeof(info));
  ret = bk_wifi_sta_get_linkstate_with_reason(&info);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (state != NULL)
    {
      *state = (int)info.state;
    }

  if (reason != NULL)
    {
      *reason = (int)info.reason_code;
    }

  return OK;
}

bool bk7258_wifi_sta_is_connected(void)
{
  int state;

  /* Same test as the vendor's own wifi_netif_sta_is_connected()
   * (cp/components/bk_wifi/src/wifi_netif.c:157-160): compare the link state
   * against CONNECTED exactly.  Note the comparison is strict, so a link that
   * has advanced to GOT_IP would read as not-connected -- the vendor keeps a
   * separate wifi_netif_sta_is_got_ip() for that.  That cannot happen here:
   * CONFIG_LWIP is unset in this profile, so nothing runs a DHCP client and
   * CONNECTED is the terminal state of the association path we implement. */

  if (bk7258_wifi_sta_connect_status(&state, NULL) < 0)
    {
      return false;
    }

  return state == WIFI_LINKSTATE_STA_CONNECTED;
}

FAR const char *bk7258_wifi_sta_state_str(int state)
{
  switch (state)
    {
      case WIFI_LINKSTATE_STA_IDLE:           return "IDLE";
      case WIFI_LINKSTATE_STA_CONNECTING:     return "CONNECTING";
      case WIFI_LINKSTATE_STA_DISCONNECTED:   return "DISCONNECTED";
      case WIFI_LINKSTATE_STA_CONNECTED:      return "CONNECTED";
      case WIFI_LINKSTATE_STA_CONNECT_FAILED: return "CONNECT_FAILED";
      case WIFI_LINKSTATE_STA_GOT_IP:         return "GOT_IP";
      case WIFI_LINKSTATE_STA_SCAN_DONE:      return "SCAN_DONE";
      default:                                return "unknown";
    }
}

FAR const char *bk7258_wifi_sta_reason_str(int reason)
{
  /* Only the codes that plausibly end a WPA2-PSK association attempt are
   * named; the caller prints the raw number too, so an unnamed code is still
   * traceable to wifi_types.h.  WIFI_REASON_MAX is the vendor's "connected
   * successfully" sentinel, not an error (wifi_types.h:246). */

  switch (reason)
    {
      case WIFI_REASON_MAX:
        return "SUCCESS";
      case WIFI_REASON_RESERVED:
        return "none";
      case WIFI_REASON_UNSPECIFIED:
        return "UNSPECIFIED";
      case WIFI_REASON_PREV_AUTH_NOT_VALID:
        return "PREV_AUTH_NOT_VALID";
      case WIFI_REASON_DEAUTH_LEAVING:
        return "DEAUTH_LEAVING";
      case WIFI_REASON_MICHAEL_MIC_FAILURE:
        return "MICHAEL_MIC_FAILURE";
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4WAY_HANDSHAKE_TIMEOUT";
      case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        return "GROUP_KEY_UPDATE_TIMEOUT";
      case WIFI_REASON_IE_IN_4WAY_DIFFERS:
        return "IE_IN_4WAY_DIFFERS";
      case WIFI_REASON_GROUP_CIPHER_NOT_VALID:
        return "GROUP_CIPHER_NOT_VALID";
      case WIFI_REASON_PAIRWISE_CIPHER_NOT_VALID:
        return "PAIRWISE_CIPHER_NOT_VALID";
      case WIFI_REASON_AKMP_NOT_VALID:
        return "AKMP_NOT_VALID";
      case WIFI_REASON_IEEE_802_1X_AUTH_FAILED:
        return "IEEE_802_1X_AUTH_FAILED";
      case WIFI_REASON_CIPHER_SUITE_REJECTED:
        return "CIPHER_SUITE_REJECTED";
      case WIFI_REASON_BEACON_LOST:
        return "BEACON_LOST";
      case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND";
      case WIFI_REASON_WRONG_PASSWORD:
        return "WRONG_PASSWORD";
      case WIFI_REASON_DISCONNECT_BY_APP:
        return "DISCONNECT_BY_APP";
      case WIFI_REASON_DHCP_TIMEOUT:
        return "DHCP_TIMEOUT";
      default:
        return "unknown";
    }
}

#else /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

int bk7258_wifi_sta_connect(FAR const char *ssid, FAR const char *psk)
{
  return -ENOSYS;
}

int bk7258_wifi_sta_connect_status(FAR int *state, FAR int *reason)
{
  return -ENOSYS;
}

bool bk7258_wifi_sta_is_connected(void)
{
  return false;
}

FAR const char *bk7258_wifi_sta_state_str(int state)
{
  return "unsupported";
}

FAR const char *bk7258_wifi_sta_reason_str(int reason)
{
  return "unsupported";
}

#endif /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

static int bk7258_wifi_initialize_once(void)
{
  int ret;

  /* bk_init.c ordering alignment: vote CPU to 120M and apply the vendor
   * calibration overlay BEFORE any wifi/phy/calibration path runs --
   * delay10us/200us loops and the calibration sequencer assume the CPU
   * is at the voted frequency, and the calibration tables must be in
   * place before calibration_init consumes them inside bk_wifi_init. */
  {
    extern bk_err_t bk_pm_module_vote_cpu_freq(uint32_t dev, uint32_t frq);
    extern void hp_dvfs_log_state(const char *tag);
    bk_err_t freq_ret;

    /* pm.h:340 PM_DEV_ID_DEFAULT == 41 and pm.h:349 PM_CPU_FRQ_120M == 3.
     * The previous (26, 2) passed PM_DEV_ID_MAC / PM_CPU_FRQ_80M -- harmless
     * only while the callee ignored its arguments. */
    hp_dvfs_log_state("pre-vote");
    freq_ret = bk_pm_module_vote_cpu_freq(41 /* PM_DEV_ID_DEFAULT */,
                                         3 /* PM_CPU_FRQ_120M */);
    hp_dvfs_log_state("post-vote");
    syslog(LOG_INFO,
           "[BK7258-WIFI] boot: cpu_freq vote 120M ret=%d\n", (int)freq_ret);
  }

  ret = bk7258_wifi_osal_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_hw_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_board_init();
  if (ret < 0)
    {
      return ret;
    }

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  {
    /* Armino runs bk_trng_driver_init() from driver_early_init()
     * (cp/middleware/driver/common/driver.c:275-277, CONFIG_TRNG_SUPPORT=y in
     * projects/app/cp/config/bk7258/config), i.e. before every driver_init()
     * peripheral including SARADC.  Keep that relative order here: this call
     * goes ahead of bk_adc_driver_init() for the same reason the AON RTC call
     * below deliberately goes after it.
     *
     * BK7258 needs no clock or power gate for this block -- authority only
     * touches sys_drv_trng_disckg_set() under CONFIG_SOC_BK7256XX, which is 0
     * in this profile (hal_port/include/common/sys_config.h:23), and bk_trng_start()
     * itself bypasses the block's clock gate (trng_ll.h:59). */

    extern void sys_hal_low_power_hardware_init(void);
    extern bk_err_t bk_trng_driver_init(void);
    extern int hp_bandgap_init(void);
    extern int bk_rand(void);
    extern bk_err_t bk_adc_driver_init(void);

    /* Armino pm_hardware_init() (components/bk_pm/pm.c:213) ->
     * sys_drv_low_power_hardware_init() (sys_ps_driver.c:191) ->
     * sys_hal_low_power_hardware_init() (sys_pm_hal.c:1508), imported whole
     * under pm/authority.  Upstream reaches it from CPU0 startup
     * (startup_cpu0.c:402), i.e. ahead of driver_early_init(), so it goes
     * first in this block.
     *
     * This is the step that establishes the analog and low-power state the
     * rest of the PM code reads back.  Most of all it runs
     * sys_hal_enable_buck(), which is the only writer of ana_reg11.aldosel --
     * the bit sys_hal_exit_low_analog() branches on to decide whether to
     * restore vanaldosel to 0x5 (LDO) or 4 (buck).  Without it that read
     * returned whatever reset left behind, so porting exit_low_analog() alone
     * aligned a leaf while leaving the code that establishes its input
     * unaligned.
     *
     * Ordering against the manual R41.lpo_config write further down is safe in
     * both directions: sys_hal_config_32k_source_default() inside this call
     * selects ROSC as well (bk_clk_32k_customer_config_get() falls through to
     * PM_LPO_SRC_ROSC here, see the divergence note below), and it touches
     * R41.wakeup_ena, a different field from lpo_config bits[1:0].  The AON
     * RTC init still runs after the lpo_config write, keeping the constraint
     * that the counter is only enabled once its clock source is settled.
     *
     * DELIBERATE DIVERGENCE: CONFIG_LPO_MP_A_FORCE_USE_EXT32K is set in the
     * authority BK7258 CP config but is left undefined here.  With it set,
     * bk_clk_32k_customer_config_get() returns PM_LPO_SRC_X32K on MP_A silicon
     * and config_32k_source_default() would switch the 32 kHz source to the
     * external crystal, powering up the XTAL and repointing the buck clock.
     * This board is verified running on ROSC, and an X32K source also shifts
     * the rwnxl_sleep threshold; switching it blind on the same change that is
     * meant to fix the analog state would confound both.  Revisit once the die
     * ID is read back and an external 32 kHz crystal is confirmed present. */

    /* sys_hal_low_power_hardware_init() used to be called here.  It now runs
     * from __start() ahead of nx_start() (bk7258_start.c), which is where the
     * authority calls it -- one line before its RTOS entry point
     * (startup_cpu0.c:402-407).  See that call site for why the position is
     * load-bearing: the function switches the AHBP power domain off, PSRAM
     * belongs to that domain, and PSRAM is 16 MiB of our heap, so calling it
     * once PSRAM had been in the heap all boot destroyed the heap contents. */

    ret = bk_trng_driver_init();
    if (ret != BK_OK)
      {
        syslog(LOG_ERR, "[BK7258-WIFI] runtime: TRNG init failed=%d\n", ret);
        bk7258_wifi_hw_deinit();
        return -ENODEV;
      }

    /* Armino bandgap_init() (components_init.c:250), ported verbatim in
     * hal_port/hal_port_bandgap.c.  Order is upstream's: components_init()
     * runs driver_early_init() -> pm_init() -> bandgap_init() ->
     * random_init(), so this sits between the TRNG init above (the
     * driver_early_init step) and the srand() below (the random_init step).
     * pm_init() is empty in this profile -- CONFIG_DEEP_PS is not set -- so
     * nothing is skipped between them.
     *
     * It reads the per-die VDDDIG bandgap trim out of OTP and programs it,
     * which this port has never done: sys_drv_set_bgcalm() was a log-only
     * stub until 2026-09-09, so the trim kept its reset value and the PHY's
     * own bandgap writes went nowhere either.  Now that the accessor is real,
     * running this makes the analog reference start from its calibrated
     * value instead of from reset -- otherwise the PHY would be adjusting a
     * trim whose baseline is wrong.
     *
     * Never fails in a way that should stop bring-up: both OTP-read failure
     * paths fall through to the default_bandgap branch and it returns BK_OK
     * regardless, so the return value is logged rather than acted on. */

    ret = hp_bandgap_init();
    syslog(LOG_INFO, "[BK7258-WIFI] runtime: bandgap trim ret=%d\n", ret);

    /* Armino random_init() (components_init.c:251, one line: srand(bk_rand())).
     * Without it rand() behaves as srand(1) per C99 7.20.2.2, so every boot
     * produces the identical sequence -- and wpa_supplicant draws the SNonce
     * and every other nonce straight from it (os_none.c:266 os_get_random(),
     * :283 os_random()).  Authority runs this after bandgap_init(); nothing
     * between the two consumes rand(), and bandgap_init() will slot in ahead
     * of this call when it is imported.
     *
     * Placement is load-bearing in a way it is not on FreeRTOS: NuttX keeps the
     * PRNG seed in task_info_s (lib_srand.c:250-259 ta_randint*), which hangs
     * off task_group_s.tg_info (sched.h:611) -- per task group, shared by the
     * pthreads in it, NOT global.  bk7258_wifi_osal_thread_create() uses
     * pthread_create(), so the vendor core/kmsg/wpas threads join this task's
     * group and observe this seed.  Seeding from a different task (a kthread,
     * or a later NSH invocation) would leave those threads on the default
     * seed while appearing to succeed. */

    srand((unsigned int)bk_rand());

    ret = bk_adc_driver_init();
    if (ret != BK_OK)
      {
        syslog(LOG_ERR, "[BK7258-WIFI] runtime: SARADC init failed=%d\n", ret);
        bk7258_wifi_hw_deinit();
        return -ENODEV;
      }
  }

  /* Armino components_init() creates the sensor cache after driver_init()
   * and before PHY users run.  This port owns SARADC initialization here, so
   * keep the same ordering locally: libbk_phy.a's manual calibration code
   * updates temperature/voltage through the sensor setters during its first
   * calibration pass and must not see an uninitialized cache. */
  ret = bk_sensor_init();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: sensor init failed=%d\n", ret);
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }

  ret = bk_event_init();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: event init failed=%d\n", ret);
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }

  /* LPO source alignment, MOVED HERE 2026-09-01 (was after
   * bk7258_wifi_lower_register, i.e. after bk_wifi_init had already run).
   *
   * The authoritative build carries CONFIG_DEFAULT_LPO_SRC=2 (= ROSC) as a
   * BUILD-TIME setting, so on that board R41.lpo_config is already ROSC
   * before any vendor library executes.  Our port programmed it late, which
   * means libbk_phy.a (bk_phy/rf_adapter_init below) and libwifi.a
   * (bk_wifi_init) both observed the reset default first and the final value
   * only afterwards.  That matters because the MM_START_REQ handler is
   * documented to call _bk_pm_lpo_src_get() twice and compare the answers:
   * a source that changes underneath the library is a divergence we control.
   *
   * Placed before bk_phy_adapter_init so BOTH archives see one stable value
   * for their whole lifetime. */
  {
    /* AON PMU R41.lpo_config (bits[1:0]): 0=DIVD 1=X32K 2=ROSC. */
    uint32_t r41 = getreg32(BK7258_AON_PMU_R41);

    r41 = (r41 & ~BK7258_AON_PMU_R41_LPO_CONFIG_MASK) | UINT32_C(2);
    putreg32(r41, BK7258_AON_PMU_R41);
    syslog(LOG_INFO,
           "[BK7258-WIFI] pmq: lpo_src set to ROSC (pre-adapter) r41=0x%08lx\n",
           (unsigned long)getreg32(BK7258_AON_PMU_R41));
  }

  /* AON RTC counter start.
   *
   * Armino runs bk_aon_rtc_driver_init() from driver_init()
   * (cp/middleware/driver/common/driver.c:380), i.e. after bk_adc_driver_init()
   * and well before app_wifi_init().  This port took over driver_init()'s
   * responsibilities above, so the SARADC-then-AON-RTC relative order is
   * preserved -- but the call is placed HERE rather than next to
   * bk_adc_driver_init() on purpose.
   *
   * The AON RTC counter is clocked from AON PMU R41.lpo_config.  The authority
   * board carries CONFIG_DEFAULT_LPO_SRC=2 as a BUILD-TIME setting, so R41
   * already selects ROSC before any authority code runs and its AON RTC init
   * always observes the final clock source.  This port programs R41 at runtime,
   * in the block immediately above.  Starting the counter before that write
   * would enable it against a source that is still about to change, which is a
   * plausible way to reproduce the very stalled-counter symptom this import
   * fixes.
   *
   * This is still far ahead of every consumer.  bk_wifi_init() below reaches
   * rwnx_intf_init() -> rwnxl_sleep(), which polls a MAC status bit and
   * compares two 64-bit tick reads to build a 200 ms timeout; with a counter
   * that never advances, that comparison keeps the loop on its fast path and
   * the timeout is never evaluated.
   */

  {
    /* TEMPORARY DIAGNOSTIC -- REMOVE once the AON RTC tick question is settled.
     * Reads the counter twice before and twice after init, so one boot log
     * answers both "was the tick frozen before?" and "does it advance after?".
     * To remove: delete this comment, the four uint64_t locals, the two
     * up_udelay() calls and the two "[aonrtc]" syslog lines, keeping the
     * bk_aon_rtc_driver_init() call and its error check.
     *
     * Reading before init is safe and non-mutating: the authority
     * aon_rtc_hal_get_counter_val() recomputes hal->hw from the unit id on
     * entry, so the still-zeroed s_aon_rtc[] state addresses the real counter
     * registers, and the accessor only reads them.
     */

    uint64_t pre_a;
    uint64_t pre_b;
    uint64_t post_a;
    uint64_t post_b;

    pre_a = bk_aon_rtc_get_current_tick(AON_RTC_ID_1);
    up_udelay(2000);
    pre_b = bk_aon_rtc_get_current_tick(AON_RTC_ID_1);

    /* 2 ms of a 32 kHz LPO is ~64 ticks, so the verdict is unambiguous:
     * two equal pre-init values mean the counter was never started. */

    syslog(LOG_INFO, "[aonrtc] pre-init tick=%llu -> %llu\n",
           (unsigned long long)pre_a, (unsigned long long)pre_b);

    ret = bk_aon_rtc_driver_init();
    if (ret != BK_OK)
      {
        syslog(LOG_ERR, "[BK7258-WIFI] runtime: AON RTC init failed=%d\n", ret);
        bk7258_wifi_hw_deinit();
        return -ENODEV;
      }

    post_a = bk_aon_rtc_get_current_tick(AON_RTC_ID_1);
    up_udelay(2000);
    post_b = bk_aon_rtc_get_current_tick(AON_RTC_ID_1);

    syslog(LOG_INFO, "[aonrtc] post-init tick=%llu -> %llu\n",
           (unsigned long long)post_a, (unsigned long long)post_b);
  }

  syslog(LOG_INFO, "[BK7258-WIFI] runtime: bind adapters\n");
  bk_phy_adapter_init();
  ret = bk_rf_adapter_init();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: RF adapter validation failed=%d\n",
             ret);
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }

  /* vnd_cal overlay: Armino runs it after app_phy_init/bk_rf_adapter_init
   * and before app_wifi_init (phy/SARADC/analog access path ready, tables
   * in place before calibration_init consumes them inside bk_wifi_init).
   * Calling it before this point crashed inside the closed-source
   * vnd_cal_set_epa_config (float logging path with uninitialized
   * infrastructure). */
  {
    extern void vnd_cal_overlay(void);

    vnd_cal_overlay();
    syslog(LOG_INFO, "[BK7258-WIFI] vnd_cal overlay applied\n");
  }
  /* power_clk_rf_init is deliberately NOT called here.
   *
   * The authoritative bk7258 build never executes it: driver.c:271 guards
   * the call with CONFIG_POWER_CLOCK_RF, Kconfig defaults it to n, and the
   * reference iperf build carries no such define (sdkconfig.cmake:429 sets
   * it empty).  ROSC calibration, temp-detect enable and the R41 bit24
   * rosc->wifi route therefore never run on the authoritative board, so
   * running them here manufactured a divergence rather than closing one.
   * See investigation/bk7258-bk-only/init-sequence-comparison.md.
   */

  syslog(LOG_INFO, "[BK7258-WIFI] runtime: bk_wifi_init begin\n");
  ret = bk_wifi_init(&(wifi_init_config_t)WIFI_DEFAULT_INIT_CONFIG());
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: bk_wifi_init failed=%d\n",
             ret);
      bk_event_deinit();
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }
  syslog(LOG_INFO, "[BK7258-WIFI] runtime: bk_wifi_init complete\n");
#endif

  ret = bk7258_wifi_lower_init(&g_bk7258_wifi);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_lower_register(&g_bk7258_wifi);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] lower register failed=%d\n", ret);
    }
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  /* Clock-root fix (see investigation external-analysis-and-clock-root-cause
   * .md §3): rwnx_env+0xa8 (the 80 MHz mode code feeding
   * rwnxl_covert_cpu_freq -> rwnxl_compute_cpu_freq -> crm_clk_set) is never
   * initialized by the pinned archive, so crm_clk_set ran with clk_config
   * row 0, leaving 0x49000000=0 and the NXMAC core without a functional
   * clock.  Both functions are global exports of libwifi.a; seeding the
   * vote slot and applying row 1 restores the authoritative clock state. */
  /* The LPO-source write that used to sit here has MOVED to just before
   * bk_phy_adapter_init (see the comment there).  Programming it at this
   * point was too late to be an alignment: bk_wifi_init had already run, so
   * libbk_phy.a and libwifi.a observed the reset default for their whole
   * init and the intended value only afterwards -- and the old comment's
   * claim "before any libwifi clock/scan path runs" was simply false. */
  /* Diagnostic experiment writes REMOVED (2026-09-01).  Three writes lived
   * here -- 0x49100054=0x10000, 0x49100010=0xDEAD0000 and
   * 0x49850010=0x108 -- and they poisoned every register table quoted as
   * parity evidence: any NXWIN/RSTWIN reading of those addresses reflected
   * our own write rather than hardware state.  The 0x49100054 write also
   * mis-attributed the bit5 divergence to ourselves, when in fact it
   * CLEARS bit5 and the closed library sets it during scan.  No
   * authoritative code writes these addresses from the host side; keep the
   * init path free of experiment writes so register evidence stays valid.
   *
   * Fix v2 seed ROLLED BACK (2026-09-01): the authoritative-board WPROBE
   * comparison proved the archive leaves rwnx_env+0xa8 at its native 0
   * through MM_START and the CRM stays on clk_config row 0
   * (0x49000000=0 / 0x49850008=0x108 / 0x49850010=0x108) while the NXMAC
   * FSM is fully active (0x49100504=0x40000000).  Seeding a8=1 and calling
   * crm_clk_set(1) here diverged from the authoritative behavior (it also
   * wrote 0x49850010=0x3108, bits[13:12] force) and is not needed: row 0
   * IS the working configuration on both platforms. */
  bk7258_wifi_parity_banner();
#endif

  /* CPU frequency alignment, tail half (2026-09-02).
   *
   * The authority votes the CPU TWICE, and the order matters:
   *   bk_init.c:267  vote(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_120M)
   *   bk_init.c:292  app_wifi_init() -> bk_netif_init() + bk_wifi_init()
   *   bk_init.c:372  #if CONFIG_CPU_DEFAULT_FREQ_60M
   *   bk_init.c:373      vote(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_60M)
   *
   * So the authority also brings Wi-Fi up at 120M and only downshifts to 60M
   * afterwards; the working reference log's `clkdiv=0x00100037` (ckdiv_core=7,
   * 60M) is the state AFTER init, not during it.  The vote at the head of this
   * function therefore stays at 120M -- replacing it with 60M would run
   * calibration and bk_wifi_init at a frequency the authority never uses.
   *
   * CONFIG_CPU_DEFAULT_FREQ_60M is 1 in the authority build
   * (build/bk7258/iperf/bk7258/config/sdkconfig.h:231), and PM_CPU_FRQ_60M is
   * 1 (pm.h:347), reusing PM_DEV_ID_DEFAULT == 41 from the 120M vote above so
   * this replaces that module's vote rather than adding a second voter. */

  {
    extern bk_err_t bk_pm_module_vote_cpu_freq(uint32_t dev, uint32_t frq);
    extern void hp_dvfs_log_state(const char *tag);
    bk_err_t freq60_ret;

    freq60_ret = bk_pm_module_vote_cpu_freq(41 /* PM_DEV_ID_DEFAULT */,
                                           1 /* PM_CPU_FRQ_60M */);
    hp_dvfs_log_state("post-init-60m");
    syslog(LOG_INFO,
           "[BK7258-WIFI] init done: cpu_freq vote 60M ret=%d\n",
           (int)freq60_ret);
  }

  return ret;
}

int bk7258_wifi_initialize(void)
{
  int expected = BK7258_WIFI_INIT_NOT_STARTED;
  int ret;

  if (!atomic_compare_exchange_strong_explicit(
        &g_bk7258_wifi_init_state, &expected,
        BK7258_WIFI_INIT_INITIALIZING, memory_order_acq_rel,
        memory_order_acquire))
    {
      if (expected == BK7258_WIFI_INIT_READY)
        {
          return OK;
        }

      if (expected == BK7258_WIFI_INIT_FAILED)
        {
          return g_bk7258_wifi_init_result;
        }

      return -EINPROGRESS;
    }

  ret = bk7258_wifi_initialize_once();
  g_bk7258_wifi_init_result = ret;
  atomic_store_explicit(&g_bk7258_wifi_init_state,
                        ret < 0 ? BK7258_WIFI_INIT_FAILED :
                                  BK7258_WIFI_INIT_READY,
                        memory_order_release);
  return ret;
}

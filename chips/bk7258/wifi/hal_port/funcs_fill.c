/* funcs_fill.c - the 27 g_wifi_os_funcs members that were declared in
 * lmac_wifi_adapter.h but left unbound in bk7258_wifi_adapter.c
 * ("missing 26" list from the alignment audit, +1 found).
 *
 * Filling policy, per the alignment strategy:
 *  - RC/AGC rx-mode members bind directly to the pinned-library exports
 *    (same treatment as _rc_drv_set_rf_en, which already resolves into
 *    libwifi.a).
 *  - _os_vsnprintf is a real OS-string mapping.
 *  - netif/IP members are NuttX-network-stack stubs: our port uses
 *    nuttx/net rather than lwIP, so the lwIP-shaped IP helpers have no
 *    equivalent; each stub logs once and returns a benign default.
 *  - EVM/RX-sensitivity and WAPI members are test/unsupported-feature
 *    stubs (STA-only scope excludes WAPI; EVM is factory-test tooling).
 *  - _get_pbuf_pool_size/_get_rx_pbuf_type return the values our pbuf
 *    shim actually honors.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

#include "lwip/pbuf.h"

/* ---------------- RC/AGC members: pinned-library exports -------------- */

extern uint32_t rc_drv_get_rx_mode_enrxsw(void);
extern void rc_drv_set_rx_mode_enrxsw(uint32_t en);
extern void rc_drv_set_agc_manual_en(uint32_t en);

uint32_t hp_rc_drv_get_rx_mode_enrxsw(void)
{
  return rc_drv_get_rx_mode_enrxsw();
}

void hp_rc_drv_set_rx_mode_enrxsw(uint32_t en)
{
  rc_drv_set_rx_mode_enrxsw(en);
}

void hp_rc_drv_set_agc_manual_en(uint32_t en)
{
  rc_drv_set_agc_manual_en(en);
}

/* ---------------- _os_vsnprintf: real OS-string mapping --------------- */

int hp_os_vsnprintf(char *buf, uint32_t size, const char *fmt, va_list ap)
{
  return vsnprintf(buf, size, fmt, ap);
}

/* ---------------- one-shot logging helper for stubs ------------------- */

static void hp_stub_log(const char *name)
{
  static unsigned long seen;

  if ((seen & (1u << 5)) == 0 || (seen++ & 0x1f) == 0)
    {
      syslog(LOG_WARNING, "[HP] funcs stub: %s\n", name);
    }
}

/* ---------------- netif/IP members (nuttx/net differs) ---------------- */

int hp_net_wlan_add_netif(uint8_t vif_idx)      /* lwip netif add -- NuttX netif is wired by the runtime app instead */
{
  hp_stub_log("net_wlan_add_netif");
  return 0;
}

int hp_net_wlan_remove_netif(uint8_t vif_idx)   /* lwip netif remove */
{
  hp_stub_log("net_wlan_remove_netif");
  return 0;
}

int hp_sta_ip_start(void)                       /* lwip DHCP start -- NuttX stack handled by runtime app */
{
  hp_stub_log("sta_ip_start");
  return 0;
}

int hp_sta_ip_down(void)                        /* lwip DHCP stop */
{
  hp_stub_log("sta_ip_down");
  return 0;
}

int hp_sta_ip_mode_set(uint32_t mode)           /* static/DHCP mode */
{
  hp_stub_log("sta_ip_mode_set");
  return 0;
}

int hp_uap_ip_start(void)                       /* soft-AP IP: out of STA scope */
{
  return 0;
}

int hp_uap_ip_down(void)
{
  return 0;
}

char *hp_inet_ntoa(uint32_t ip)                 /* caller-held static buffer in lwIP port; not used on STA data path */
{
  hp_stub_log("inet_ntoa");
  return "0.0.0.0";
}

uint32_t hp_lookup_ipaddr(const char *name)     /* DNS lookup -- not on scan path */
{
  hp_stub_log("lookup_ipaddr");
  return 0;
}

void hp_get_net_info(void *info)                /* station link info struct */
{
  hp_stub_log("get_net_info");
}

void hp_save_net_info(void *netif, void *sta)   /* save ip/netmask/gw before IP change */
{
  hp_stub_log("save_net_info");
}

int hp_set_sta_status(uint32_t status)          /* STA up/down notify into lwip */
{
  hp_stub_log("set_sta_status");
  return 0;
}

/* ---------------- EVM / RX-sensitivity (factory test tools) ----------- */

void hp_do_evm(void *param)                     { hp_stub_log("do_evm"); }
void hp_do_rx_sensitivity(void)                 { hp_stub_log("do_rx_sensitivity"); }
void hp_evm_via_mac_evt(void)                   { }
void hp_evm_via_mac_continue(void)              { }
uint32_t hp_tx_evm_rate_get(void)               { return 0; }
uint32_t hp_tx_evm_bandwidth_get(void)          { return 0; }
uint32_t hp_tx_evm_mode_get(void)               { return 0; }
uint32_t hp_tx_evm_guard_i_tpye_get(void)       { return 0; }
uint32_t hp_tx_evm_modul_format_get(void)       { return 0; }
uint32_t hp_tx_evm_pwr_idx_get(void)            { return 0; }

/* ---------------- WAPI (out of STA-only scope) ------------------------- */

int hp_wapi_wpi_encrypt(void *param)            { return -1; }
int hp_wapi_wpi_decrypt(void *param)            { return -1; }

/* ---------------- pbuf sizing questions ------------------------------- */

uint32_t hp_get_pbuf_pool_size(void)
{
  return 0; /* no PBUF_POOL pool: our shim allocates from heap */
}

uint32_t hp_get_rx_pbuf_type(void)
{
  return (uint32_t)PBUF_RAM; /* fhost_rxbuf_push allocates PBUF_RAM pbufs */
}

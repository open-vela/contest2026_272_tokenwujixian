/* WPA handler queue: the object definition plus its three accessors.
 *
 * Upstream keeps these in hostapd/main_none.c, but they are not SoftAP
 * functionality -- they are the message queue shared by wpa_supplicant and
 * hostapd, exactly like hapd_intf_ke_rx_handle(). The STA path cannot run
 * without them:
 *
 *   wpah_queue                 eloop.c:680 pops the WPA event loop's messages
 *   wpa_hostapd_queue_command  ctrl_iface.c:108 (__wpa_ctrl_request) posts
 *                              every STA control request
 *   wpa_hostapd_queue_poll     sk_intf.c:99 (ke_mgmt_packet_tx) and
 *                              wpa_supplicant.c:7881 (wpa_supplicant_run)
 *                              wake the WPA thread after MLME TX
 *   is_wpah_queue_full         rwnx_rx.c:316,360 applies RX backpressure
 *
 * Compiling main_none.c to obtain them would pull in the whole hostapd
 * provider closure (hostapd_config_defaults, hostapd_init,
 * hostapd_setup_interface, wpa_drivers, os_program_init, ...), which is the
 * SoftAP surface this milestone deliberately leaves out. Since the four
 * primitives are small and their semantics are fully visible in the vendor
 * source, own them here instead. This mirrors net_param_shim.c.
 *
 * Ownership split, matching upstream:
 *   - lifecycle: main_supplicant.c creates the queue (sizeof(wpah_msg_t) x 64,
 *     line 214) and destroys it (line 201); it declares this variable extern at
 *     line 61. This file only defines the storage.
 *   - transport: the rtos_* queue API, already backed by NuttX message queues
 *     in hal_port/rtos_compat_shim.c.
 *
 * Behaviour is copied from the vendor implementation, not invented:
 * both posts use BEKEN_NO_WAIT so no caller can block on a full queue, poll
 * sends WPA_CTRL_CMD_SOCKET, and a NULL queue is reported rather than treated
 * as success.
 */

#include <bk_prelude.h>

#include "utils/eloop.h"
#include "wpa_ctrl.h"
#include <wpa_compat/wpa_err.h>

/* Log through BK_LOGW rather than WPA_LOGW.
 *
 * WPA_LOGW is just `BK_LOGW("wpa", ...)` (wpa_debug.h:25), and BK_LOGW is
 * already in scope here through bk_prelude.h -> components/system.h ->
 * common/bk_err.h -> components/log.h. Including wpa_debug.h to get the alias
 * would also pull in wpabuf.h, which needs PRINTF_FORMAT from utils/common.h;
 * without that prerequisite the header fails to parse, and adding common.h to a
 * team-owned shim drags in the whole WPA utility surface for one log line.
 */

#define BK7258_WPA_QUEUE_TAG "wpa"

/* Defined here, declared extern by main_supplicant.c:61 and eloop.c:29. */

beken_queue_t wpah_queue = NULL;

int wpa_hostapd_queue_command(wpah_msg_t *msg)
{
  int ret;

  if (wpah_queue == NULL)
    {
      return WPA_ERR_WPAH_QUEUE_INIT;
    }

  ret = rtos_push_to_queue(&wpah_queue, msg, BEKEN_NO_WAIT);
  if (ret != kNoErr)
    {
      /* Non-fatal: the caller maps a failed post onto its own error path. */

      BK_LOGW(BK7258_WPA_QUEUE_TAG,
              "wpa_hostapd_queue_command:%d\r\n", ret);
    }

  return ret;
}

uint32_t wpa_hostapd_queue_poll(uint32_t param)
{
  wpah_msg_t msg = {0};
  int ret = 0;

  if (wpah_queue == NULL)
    {
      return ret;
    }

  msg.cmd = WPA_CTRL_CMD_SOCKET;
  msg.argu = param;

  ret = rtos_push_to_queue(&wpah_queue, &msg, BEKEN_NO_WAIT);
  if (ret != kNoErr)
    {
      BK_LOGW(BK7258_WPA_QUEUE_TAG,
              "wpa_hostapd_queue_poll:%d\r\n", ret);
    }

  return (uint32_t)ret;
}

bool is_wpah_queue_full(void)
{
  /* rwnx_rx.c calls this from the RX path before queueing a management frame,
   * so an uninitialised queue must read as full: there is no consumer yet.
   */

  if (wpah_queue == NULL)
    {
      return true;
    }

  return rtos_is_queue_full(&wpah_queue);
}

/* Channel-switch announcement, transcribed verbatim from the vendor's
 * hostapd/main_none.c:1228 and :1233.
 *
 * Unlike the queue primitives above, these two ARE SoftAP functionality, so
 * they are here for a different reason: linkage, not behaviour.  wifi_v2.c is
 * compiled as a whole translation unit, and its bk_wlan_ap_csa_coexist_mode()
 * (:368) and the CSA path at :5286/:5310 reference these names.  Once anything
 * in the STA path keeps wifi_v2.c's objects alive, the linker must resolve them
 * even though no STA flow can reach them -- "never executed" is not "never
 * linked".  Compiling main_none.c to obtain them is not an option for the
 * reason already given at the top of this file.
 *
 * They are one-line forwards in the vendor source and their only dependency,
 * wpa_ctrl_request_async(), is already built here (ctrl_iface.c:138), with both
 * command codes declared in wpa_ctrl.h:572-573.  So this is a transcription,
 * not a stub: if a SoftAP CSA path is ever enabled, the behaviour is already
 * the vendor's.
 *
 * Prototypes are repeated locally on purpose.  The vendored declarations live
 * in third_party/.../hostapd/main_none.h:21-22, but "main_none.h" resolves to
 * hal_port/include/wpa_compat/main_none.h first, and that header deliberately
 * declares no SoftAP entry points.
 */

int hostapd_channel_switch(int new_freq);
int hostapd_channel_switch_stop(void);

int hostapd_channel_switch(int new_freq)
{
  return wpa_ctrl_request_async(WPA_CTRL_CMD_AP_CHAN_SWITCH,
                                (void *)new_freq);
}

int hostapd_channel_switch_stop(void)
{
  return wpa_ctrl_request_async(WPA_CTRL_CMD_AP_CHAN_SWITCH_STOP, NULL);
}

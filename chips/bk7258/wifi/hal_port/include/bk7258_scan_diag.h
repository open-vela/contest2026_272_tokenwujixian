/*
 * Per-scan, privacy-preserving diagnostics for the BK7258 STA scan path.
 *
 * These counters deliberately contain no frame bytes, SSIDs, BSSIDs, scan
 * IDs, credentials, or association state.  They identify the boundary where
 * a scan stops making progress: requested active channels, LMAC result
 * indications, host result parsing, or the independent host-management RX
 * route.
 */

#ifndef __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H
#define __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H

#include <stdint.h>

struct bk7258_scan_diag_s
{
  uint32_t start_calls;
  uint32_t start_accepted;
  uint32_t start_rejected;
  uint32_t request_build_fail;
  uint32_t requested_channels;
  uint32_t active_channels;
  uint32_t passive_channels;
  uint32_t lmac_complete;
  uint32_t lmac_result_ind;
  uint32_t result_inserted;
  uint32_t result_table_full;
  uint32_t result_country_drop;
  uint32_t result_duplicate;
  uint32_t result_alloc_fail;
  uint32_t host_mgmt_beacon;
  uint32_t host_mgmt_probe_resp;
  uint32_t host_mgmt_no_sta_vif;
  uint32_t host_mgmt_wpaq_drop;
  uint32_t host_mgmt_wpaq_forwarded;
  uint32_t completion_callback;
  uint32_t result_fetch_fail;
  uint32_t result_exported;
};

enum bk7258_scan_diag_event_e
{
  BK7258_SCAN_DIAG_START_CALL,
  BK7258_SCAN_DIAG_START_ACCEPTED,
  BK7258_SCAN_DIAG_START_REJECTED,
  BK7258_SCAN_DIAG_REQUEST_BUILD_FAIL,
  BK7258_SCAN_DIAG_LMAC_COMPLETE,
  BK7258_SCAN_DIAG_LMAC_RESULT_IND,
  BK7258_SCAN_DIAG_RESULT_INSERTED,
  BK7258_SCAN_DIAG_RESULT_TABLE_FULL,
  BK7258_SCAN_DIAG_RESULT_COUNTRY_DROP,
  BK7258_SCAN_DIAG_RESULT_DUPLICATE,
  BK7258_SCAN_DIAG_RESULT_ALLOC_FAIL,
  BK7258_SCAN_DIAG_HOST_MGMT_BEACON,
  BK7258_SCAN_DIAG_HOST_MGMT_PROBE_RESP,
  BK7258_SCAN_DIAG_HOST_MGMT_NO_STA_VIF,
  BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_DROP,
  BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_FORWARDED,
  BK7258_SCAN_DIAG_COMPLETION_CALLBACK,
  BK7258_SCAN_DIAG_RESULT_FETCH_FAIL,
};

/* The former g_mmstart_* latch globals were removed 2026-09-01: the
 * bk7258_hwprobe_* facility below carries the same seen/ke_state/ret/r38
 * values across four sample points instead of two, and reports them through
 * bk7258_hwprobe_report(), so nothing read those globals any more. */

/****************************************************************************
 * MM bring-up handshake latches
 *
 * Four sample points around the MM_RESET / MM_START handshake, recording only
 * memory-resident facts: which site was reached (`seen`), ke_state_get(TASK_MM)
 * as read in the caller's own translation unit, and the rw_msg_send() return
 * code.  Pass kestate = -1 at the two reset sites, which have no MM task state
 * worth reporting, and ret = 0 on the two `pre` sites.
 *
 * THIS FACILITY NO LONGER READS ANY REGISTER, AND MUST NOT REGAIN ONE.
 * It used to snapshot 13 words from NXMAC (0x49100000), the MAC interrupt block
 * (0x49108000), the modem clock (0x49000000) and CRM (0x49850000).  All of
 * those sit behind the MAC clock gate, and with CONFIG_PM_V2 && CONFIG_STA_PS
 * compiled in (as here, and as on the authority CP) the MAC is in doze from the
 * end of bk_wifi_init() onward -- rwnx_intf_init() calls rwnxl_sleep()
 * (rw_task.c:1245) -- and is awake only inside the windows
 * mac_wakeup_and_pwr_update() opens on the core thread (rw_task.c:821).  These
 * four latch sites run on the supplicant/app thread via
 * sa_station_cfg80211_init() (sa_station.c:128,139), so they hold no wake, and
 * a load from a gated register stalls the AHB permanently: no fault, no log, no
 * other thread scheduled.  The full reasoning, and the board evidence that the
 * register columns were uninformative anyway, is in hal_port/scan_diagnostics.c.
 *
 * The "safety note" that used to stand here -- that these addresses had been
 * read at these very points on hardware without faulting -- was true only of
 * the pre-STA_PS configuration, where rwnxl_sleep() was not compiled and the
 * MAC never dozed.  Do not restore it as a licence.
 *
 * Latching rather than printing in place stays deliberate: printing from those
 * contexts raced with other threads' console output and destroyed the
 * measurement.  The four one-line calls in the vendored rw_msg_tx.c stay for
 * the reason they were introduced -- we never call rw_msg_send_reset() /
 * rw_msg_send_start() ourselves -- and are now stall-free by construction.
 ****************************************************************************/

enum bk7258_hwprobe_site_e
{
  BK7258_HWPROBE_PRE_RESET = 0,
  BK7258_HWPROBE_POST_RESET,
  BK7258_HWPROBE_PRE_START,
  BK7258_HWPROBE_POST_START,
  BK7258_HWPROBE_NSITES
};

void bk7258_hwprobe_latch(enum bk7258_hwprobe_site_e site,
                          int kestate, int ret);
void bk7258_hwprobe_report(void);

void bk7258_scan_diag_begin(void);
void bk7258_scan_diag_request_channels(uint32_t requested, uint32_t active,
                                       uint32_t passive);
void bk7258_scan_diag_record(enum bk7258_scan_diag_event_e event);
void bk7258_scan_diag_result_exported(uint32_t count);
void bk7258_scan_diag_snapshot(struct bk7258_scan_diag_s *snapshot);

#endif /* __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H */

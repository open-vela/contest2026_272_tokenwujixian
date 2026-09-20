/* See bk7258_scan_diag.h. */

#include <nuttx/config.h>
#include <nuttx/spinlock.h>

#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include "bk7258_scan_diag.h"

static struct bk7258_scan_diag_s g_scan_diag;
static spinlock_t g_scan_diag_lock = SP_UNLOCKED;

void bk7258_scan_diag_begin(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);

  memset(&g_scan_diag, 0, sizeof(g_scan_diag));
  g_scan_diag.start_calls = 1;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_request_channels(uint32_t requested, uint32_t active,
                                       uint32_t passive)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);

  g_scan_diag.requested_channels = requested;
  g_scan_diag.active_channels = active;
  g_scan_diag.passive_channels = passive;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_record(enum bk7258_scan_diag_event_e event)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);
  uint32_t *counter = NULL;

  switch (event)
    {
      case BK7258_SCAN_DIAG_START_CALL:
        counter = &g_scan_diag.start_calls;
        break;
      case BK7258_SCAN_DIAG_START_ACCEPTED:
        counter = &g_scan_diag.start_accepted;
        break;
      case BK7258_SCAN_DIAG_START_REJECTED:
        counter = &g_scan_diag.start_rejected;
        break;
      case BK7258_SCAN_DIAG_REQUEST_BUILD_FAIL:
        counter = &g_scan_diag.request_build_fail;
        break;
      case BK7258_SCAN_DIAG_LMAC_COMPLETE:
        counter = &g_scan_diag.lmac_complete;
        break;
      case BK7258_SCAN_DIAG_LMAC_RESULT_IND:
        counter = &g_scan_diag.lmac_result_ind;
        break;
      case BK7258_SCAN_DIAG_RESULT_INSERTED:
        counter = &g_scan_diag.result_inserted;
        break;
      case BK7258_SCAN_DIAG_RESULT_TABLE_FULL:
        counter = &g_scan_diag.result_table_full;
        break;
      case BK7258_SCAN_DIAG_RESULT_COUNTRY_DROP:
        counter = &g_scan_diag.result_country_drop;
        break;
      case BK7258_SCAN_DIAG_RESULT_DUPLICATE:
        counter = &g_scan_diag.result_duplicate;
        break;
      case BK7258_SCAN_DIAG_RESULT_ALLOC_FAIL:
        counter = &g_scan_diag.result_alloc_fail;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_BEACON:
        counter = &g_scan_diag.host_mgmt_beacon;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_PROBE_RESP:
        counter = &g_scan_diag.host_mgmt_probe_resp;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_NO_STA_VIF:
        counter = &g_scan_diag.host_mgmt_no_sta_vif;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_DROP:
        counter = &g_scan_diag.host_mgmt_wpaq_drop;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_FORWARDED:
        counter = &g_scan_diag.host_mgmt_wpaq_forwarded;
        break;
      case BK7258_SCAN_DIAG_COMPLETION_CALLBACK:
        counter = &g_scan_diag.completion_callback;
        break;
      case BK7258_SCAN_DIAG_RESULT_FETCH_FAIL:
        counter = &g_scan_diag.result_fetch_fail;
        break;
    }

  if (counter != NULL)
    {
      (*counter)++;
    }

  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_result_exported(uint32_t count)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);

  g_scan_diag.result_exported = count;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

/****************************************************************************
 * Hardware register snapshots -- see the contract note in bk7258_scan_diag.h.
 *
 * Migrated here 2026-09-01 from inline probes that lived in two vendored
 * files (third_party/.../rw_msg_tx.c and sa_station.c, ~140 lines between
 * them).  Both sampled the same four moments, because sa_station's probes
 * bracketed the very calls that now latch internally, so folding them
 * together let sa_station.c go back to byte-identical vendor source.
 ****************************************************************************/

/* ALL THIRTEEN MMIO READS WERE REMOVED FROM THIS FACILITY (2026-09-08).
 *
 * What they were: 0x49100000/0010/0038/0054/0500/0504/0508 (NXMAC),
 * 0x49000000 (modem clock), 0x49850008/0010 (CRM) and
 * 0x49108000+0xf4/0x7c/0x70 (MAC interrupt block), latched at the four
 * corners of the MM_RESET / MM_START handshake and printed as rows hp1..hpd.
 *
 * Why they cannot stay.  Every one of those addresses is behind the MAC's
 * clock gate.  With CONFIG_PM_V2 && CONFIG_STA_PS (sys_config.h:69,70 in this
 * tree, and y on the authority CP) rwnx_intf_init() ends in rwnxl_sleep()
 * (rw_task.c:1245), so the MAC is in doze from the end of bk_wifi_init()
 * onward.  It is re-woken only by mac_wakeup_and_pwr_update() (rw_task.c:821)
 * at the head of the core thread's message loop, i.e. for the duration of one
 * dequeued BMSG, on the core thread.
 *
 * The four latch sites are NOT on the core thread: rw_msg_send_reset() and
 * rw_msg_send_start() are called from sa_station_cfg80211_init()
 * (sa_station.c:128,139) on the supplicant/app thread, which only enqueues to
 * the ke task.  So at PRE_RESET the MAC is in doze with certainty -- that is
 * the first thing that happens after init -- and none of the other three sites
 * has a wake guarantee either.  A load from a gated register stalls the AHB
 * permanently: the CPU never retires the instruction, nothing faults
 * (CONFIG_DEBUG_BUSFAULT reports bus errors, not stalls), and no other thread
 * ever runs to print anything.  This is the identical failure the parity
 * banner's snapshot produced on hardware.
 *
 * Why nothing of value is lost.  The four register columns were already known
 * to be uninformative: see the note above bk7258_hwprobe_report()'s call site
 * in bk7258_wifi_lower.c, which records the board evidence that all four
 * columns read r38=0 / ret=0 while MM had in fact started (mmstate=1,
 * r38=0x33 at end of scan), because mm_start_req_handler sends its CFM before
 * mm_active() and then parks the MAC.  The authority takes no such reading at
 * these points at all.
 *
 * What is KEPT is the part that was always the load-bearing one, and is
 * stall-free because it is plain memory: `seen` (which of the four sites was
 * reached), `kestate` (ke_state_get(TASK_MM) -- libwifi's own SRAM state
 * variable, read in the caller's TU) and `ret` (the rw_msg_send() result).
 * This is why the four one-line calls in the vendored rw_msg_tx.c are left
 * untouched rather than reverted to pristine authority source: with the MMIO
 * gone they can no longer stall, and they still answer "was MM_RESET /
 * MM_START reached, and what did it return".
 *
 * If MAC registers must be sampled around this handshake again, do it from
 * inside the core thread while it is servicing a BMSG (post
 * mac_wakeup_and_pwr_update), never from the requesting thread, and never
 * guard such a read by first reading another MAC register -- that guard is
 * itself the stall.  The only stall-free status sources are the always-on SYS
 * domain (0x44010000), AON PMU, and libwifi's own RAM.
 */

struct bk7258_hwprobe_s
{
  int16_t  kestate;     /* ke_state_get(TASK_MM), -1 when the site has none   */
  int16_t  ret;         /* rw_msg_send() result, 0 when the site has none     */
  uint8_t  seen;
};

static struct bk7258_hwprobe_s g_hwprobe[BK7258_HWPROBE_NSITES];

void bk7258_hwprobe_latch(enum bk7258_hwprobe_site_e site,
                          int kestate, int ret)
{
  struct bk7258_hwprobe_s snap;
  irqstate_t flags;

  if ((unsigned)site >= BK7258_HWPROBE_NSITES)
    {
      return;
    }

  snap.kestate = (int16_t)kestate;
  snap.ret     = (int16_t)ret;
  snap.seen    = 1;

  flags = spin_lock_irqsave(&g_scan_diag_lock);
  g_hwprobe[site] = snap;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_hwprobe_report(void)
{
  struct bk7258_hwprobe_s s[BK7258_HWPROBE_NSITES];
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_scan_diag_lock);
  memcpy(s, g_hwprobe, sizeof(s));
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);

  /* seen tells which of the four latch points were actually reached; without
   * it a zeroed entry cannot be told from one that was never sampled. */

  syslog(LOG_INFO, "[BK7258-WIFI] hp0 seen=%u%u%u%u ke=%d,%d ret=%d,%d\n",
         (unsigned)s[0].seen, (unsigned)s[1].seen,
         (unsigned)s[2].seen, (unsigned)s[3].seen,
         (int)s[2].kestate, (int)s[3].kestate,
         (int)s[1].ret, (int)s[3].ret);
}

void bk7258_scan_diag_snapshot(struct bk7258_scan_diag_s *snapshot)
{
  irqstate_t flags;

  if (snapshot == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&g_scan_diag_lock);
  memcpy(snapshot, &g_scan_diag, sizeof(*snapshot));
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

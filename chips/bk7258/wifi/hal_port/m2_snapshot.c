/*
 * Correlated read-only MAC/PHY state snapshots for EAPOL TX confirmation.
 *
 * MAC, modem clock, CRM and MAC IRQ words are the board-read whitelist used
 * by scan_diagnostics.c. AGC/RC/TRX are raw words at documented BK7258 bases.
 */

#include <nuttx/config.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/syslog/syslog.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bk7258_m2_snapshot.h"

#define BK7258_M2_SNAPSHOT_SLOTS 4

/* THE ELEVEN-REGISTER HARDWARE SNAPSHOT WAS REMOVED (2026-09-08).
 *
 * It sampled the MAC (0x49100038/0054), the modem clock (0x49000000), CRM
 * (0x49850008/0010), the MAC interrupt block (0x49108070/007c/00f4) and three
 * PHY words (0x4980a000 AGC, 0x4980c000 RC, 0x4980c200 TRX) at EAPOL submit
 * and again at confirm, and printed the submit>confirm delta.
 *
 * All eleven live in the modem/PHY power+clock domain, which is exactly the
 * domain rwnxl_sleep() gates.  This tree compiles CONFIG_PM_V2 &&
 * CONFIG_STA_PS (sys_config.h:69,70), so rwnx_intf_init() puts the MAC into
 * doze at the end of bk_wifi_init() (rw_task.c:1245) and it is only awake
 * inside the windows mac_wakeup_and_pwr_update() opens on the core thread
 * (rw_task.c:821).  A load from a gated register does not fault -- it stalls
 * the AHB forever, wedging the CPU mid-instruction with no fault, no log and
 * no other thread able to run.  The same pattern in bk7258_wifi_parity_banner()
 * was reproduced and fixed on hardware.
 *
 * Per-site wake analysis, which is why BOTH halves had to go and not just one:
 *
 *   submit (fhost_tx_cfm/rwnx_tx submit path, rwnx_tx.c:934) -- reached from
 *     bmsg_tx_handler() on the core thread, i.e. after that loop iteration's
 *     mac_wakeup_and_pwr_update().  The MAC IS awake here.  This site alone
 *     was safe.
 *
 *   confirm (fhost_tx_cfm_push, rwnx_tx.c:1314) -- driven by the MAC TX
 *     confirm path, and dispatch may be deferred (HISR/workqueue), so the
 *     wake window that carried the transmit can already have closed by the
 *     time this runs.  NOT safe.
 *
 * A submit-only snapshot has no diagnostic value: the whole construct existed
 * to show what changed between submit and confirm.  So the safe half is
 * useless without the unsafe half, and the correct resolution is to drop the
 * register sampling entirely rather than keep a guarded remnant.
 *
 * What remains is the part that answered the actual question ("did this EAPOL
 * descriptor reach a confirm, and with what status/queue/ids") and is plain
 * memory, so it cannot stall: the submit-time scalars correlated to the
 * confirm-time status.
 *
 * If MAC/PHY state around EAPOL TX is needed again: sample it only from the
 * core thread inside a BMSG it is servicing, and never build a "is the MAC
 * awake" guard out of a MAC register read -- that read is itself the stall.
 * Stall-free status sources are the always-on SYS domain (0x44010000), AON
 * PMU, and libwifi's own RAM (ke_state_get(), ps_env flags).
 */

struct bk7258_m2_snapshot_s
{
  struct txdesc *txdesc;
  uint32_t id;
  uint32_t flags;
  uint8_t queue;
  uint8_t tid;
  uint8_t vif;
  uint8_t staid;
};

static struct bk7258_m2_snapshot_s
  g_bk7258_m2_snapshot[BK7258_M2_SNAPSHOT_SLOTS];
static spinlock_t g_bk7258_m2_snapshot_lock = SP_UNLOCKED;
static uint32_t g_bk7258_m2_snapshot_next_id;

void bk7258_m2_snapshot_submit(struct txdesc *txdesc, uint8_t queue,
                               uint8_t tid, uint8_t vif, uint8_t staid,
                               uint32_t flags)
{
  struct bk7258_m2_snapshot_s snapshot;
  irqstate_t irqstate;
  unsigned int index;
  unsigned int empty = BK7258_M2_SNAPSHOT_SLOTS;

  if (txdesc == NULL)
    {
      return;
    }

  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.txdesc = txdesc;
  snapshot.queue = queue;
  snapshot.tid = tid;
  snapshot.vif = vif;
  snapshot.staid = staid;
  snapshot.flags = flags;

  irqstate = spin_lock_irqsave(&g_bk7258_m2_snapshot_lock);
  for (index = 0; index < BK7258_M2_SNAPSHOT_SLOTS; index++)
    {
      if (g_bk7258_m2_snapshot[index].txdesc == txdesc)
        {
          empty = index;
          break;
        }

      if (g_bk7258_m2_snapshot[index].txdesc == NULL &&
          empty == BK7258_M2_SNAPSHOT_SLOTS)
        {
          empty = index;
        }
    }

  if (empty < BK7258_M2_SNAPSHOT_SLOTS)
    {
      snapshot.id = g_bk7258_m2_snapshot_next_id++;
      g_bk7258_m2_snapshot[empty] = snapshot;
    }

  spin_unlock_irqrestore(&g_bk7258_m2_snapshot_lock, irqstate);
}

void bk7258_m2_snapshot_cfm(struct txdesc *txdesc, uint8_t queue,
                            uint32_t status)
{
  struct bk7258_m2_snapshot_s snapshot;
  irqstate_t irqstate;
  bool found = false;
  unsigned int index;

  if (txdesc == NULL)
    {
      return;
    }

  irqstate = spin_lock_irqsave(&g_bk7258_m2_snapshot_lock);
  for (index = 0; index < BK7258_M2_SNAPSHOT_SLOTS; index++)
    {
      if (g_bk7258_m2_snapshot[index].txdesc == txdesc)
        {
          snapshot = g_bk7258_m2_snapshot[index];
          g_bk7258_m2_snapshot[index].txdesc = NULL;
          found = true;
          break;
        }
    }

  spin_unlock_irqrestore(&g_bk7258_m2_snapshot_lock, irqstate);
  if (!found)
    {
      return;
    }

  syslog(LOG_WARNING,
         "[BK7258-M2] id=%lu cfm=%08lx q=%u/%u tid=%u vif=%u sta=%u fl=%08lx\n",
         (unsigned long)snapshot.id, (unsigned long)status,
         (unsigned)snapshot.queue, (unsigned)queue, (unsigned)snapshot.tid,
         (unsigned)snapshot.vif, (unsigned)snapshot.staid,
         (unsigned long)snapshot.flags);

  /* The three register-delta lines that followed (macfsm/state/clk, crm/irq,
   * phy agc/rc/trx) went with the sampling they printed -- see the note at
   * the head of this file. */
}

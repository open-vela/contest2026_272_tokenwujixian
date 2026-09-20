/*
 * Read-only EAPOL TX snapshots for comparing an associated OpenVela station
 * with the matching Armino station image. No frame payload, peer address,
 * SSID, or credential data is retained or printed.
 *
 * Scalars only, as of 2026-09-08: the eleven MAC/modem/CRM/PHY register words
 * this used to sample at submit and confirm were removed because they sit
 * behind the MAC clock gate, which CONFIG_PM_V2 + CONFIG_STA_PS closes between
 * wake windows, and reading a gated register stalls the AHB with no fault.
 * See the note at the head of hal_port/m2_snapshot.c before adding any register
 * read back to either entry point.
 */

#ifndef __BK7258_WIFI_GLUE_BK7258_M2_SNAPSHOT_H
#define __BK7258_WIFI_GLUE_BK7258_M2_SNAPSHOT_H

#include <stdint.h>

struct txdesc;

/* Called after fhost_txdesc_init(), before the descriptor is queued. */
void bk7258_m2_snapshot_submit(struct txdesc *txdesc, uint8_t queue,
                               uint8_t tid, uint8_t vif, uint8_t staid,
                               uint32_t flags);

/* Called for a terminal TX confirmation with its MAC-owned status word. */
void bk7258_m2_snapshot_cfm(struct txdesc *txdesc, uint8_t queue,
                            uint32_t status);

#endif /* __BK7258_WIFI_GLUE_BK7258_M2_SNAPSHOT_H */

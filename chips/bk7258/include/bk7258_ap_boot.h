/****************************************************************************
 * chips/bk7258/include/bk7258_ap_boot.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_AP_BOOT_H
#define __VENDOR_BEKEN_CHIP_BK7258_AP_BOOT_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#define BK7258_AP_BOOT_MAGIC       UINT32_C(0x4150564c) /* "APVL" */
#define BK7258_AP_BOOT_ABI_VERSION UINT32_C(1)
#define BK7258_AP_BUILD_ID         UINT32_C(0x4f564131) /* "OVA1" */
#define BK7258_AP_FAULT_PANIC_BASE UINT32_C(0x50414e00) /* "PAN" + action */
#define BK7258_AP_FAULT_SLEEP_BASE UINT32_C(0x534c5000) /* "SLP" + errno */
#define BK7258_AP_FAULT_C2START_BASE UINT32_C(0x43325400) /* "C2T" + status */

#define BK7258_AP_IMAGE_CONTRACT_OFFSET UINT32_C(0x200)
#define BK7258_AP_IMAGE_MAGIC           UINT32_C(0x4f564150) /* "OVAP" */

struct bk7258_ap_image_contract_s
{
  uint32_t magic;
  uint32_t abi_version;
  uint32_t build_id;
  uint32_t vector_base;
};

enum bk7258_ap_stage_e
{
  BK7258_AP_STAGE_EMPTY = 0,
  BK7258_AP_STAGE_CP_PREPARED = 1,
  BK7258_AP_STAGE_RESET_ENTERED = 2,
  BK7258_AP_STAGE_SCHEDULER_RUNNING = 3,
  BK7258_AP_STAGE_FAULT = 0xff
};

/* This record occupies the first 64 bytes of the locked 2 KiB SWAP region.
 * CP is the only writer before CPU1 reset release; AP is the only writer after
 * release.  commit is a seqlock: odd means an update is in progress, even
 * means readers may verify the checksum and accept a stable snapshot.
 */

struct bk7258_ap_boot_record_s
{
  uint32_t commit;
  uint32_t magic;
  uint32_t abi_version;
  uint32_t size;
  uint32_t boot_sequence;
  uint32_t stage;
  uint32_t heartbeat;
  uint32_t fault;
  uint32_t vector_base;
  uint32_t reset_pc;
  uint32_t build_id;
  uint32_t reserved[4];
  uint32_t checksum;
};

uint32_t bk7258_ap_record_prepare(void);
bool bk7258_ap_record_read(struct bk7258_ap_boot_record_s *record);
void bk7258_ap_record_mark_reset(uint32_t vector_base, uint32_t reset_pc);
void bk7258_ap_record_mark_scheduler(void);
void bk7258_ap_record_heartbeat(void);
void bk7258_ap_record_fault(uint32_t fault);

/* CPU2 handshake flag: up_cpu_start() clears the SWAP spare word before
 * releasing physical CPU2, and bk7258_cpu2_boot() sets it once its early boot
 * is complete.  CPU1 then polls it with a bounded loop.  This replaces the
 * boot spinlock handshake so that SMP bring-up does not depend on the
 * cross-core exclusive-monitor semantics of the BK7258 SRAM, which remain an
 * unverified A3 item for the runtime scheduler spinlocks.
 *
 * SWAP word map (word = 4 bytes from BK7258_SWAP_BASE):
 *   words  0..15 : ABI boot record (64 B, seqlock protected);
 *   words 16..23 : the two generation slots used by sequence recovery --
 *                  the flag word MUST sit behind them so a handshake write
 *                  can never destroy the boot sequence history;
 *   word  44     : CPU2 handshake flag (see below).
 */

#define BK7258_AP_SWAP_C2_WORD    UINT32_C(44)

#define BK7258_AP_SWAP_C2_MAGIC   UINT32_C(0x4332414c) /* "C2AL" */
void bk7258_ap_c2flag_clear(void);
void bk7258_ap_c2flag_set(void);
bool bk7258_ap_c2flag_test(void);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_AP_BOOT_H */

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
#define BK7258_AP_FAULT_MBOX_BASE UINT32_C(0x4d425800) /* "MBX" + fstat */

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

/* Debug boot-progress markers written by the AP cores into SWAP word 40.
 * The CP monitor prints marker changes so a silent bring-up stall can be
 * attributed to a specific core/step.  Bring-up diagnostics only: removed
 * once the SMP start path is stable on hardware. */

#define BK7258_AP_SWAP_DBG_WORD         UINT32_C(40)

/* CPU1 scheduler gate (v5): CPU0 keeps CPU2 out of the shared scheduler
 * until the early-board threads (audio sync init + amp-init/ap-health
 * kthread creation, bk7258_ap_initialize()) are fully constructed.
 * bk7258_cpu2_boot() spins here BEFORE arming the Mailbox IPI and SysTick,
 * so CPU2 cannot be woken by an IPI nor tick into nxsched_process_timer()
 * while CPU0 is still creating the kernel/board heap objects both cores
 * will climb concurrently.  CPU0 raises the gate with
 * bk7258_ap_sched_gate_set() at the end of bk7258_ap_initialize().
 *
 * SWAP word map: word 56 is free between the UART panic token (word 50)
 * and the AP panic record (SWAP offset 0x100, word 64+).  The bit pattern
 * spells "GATE". */

#define BK7258_AP_SWAP_GATE_WORD  UINT32_C(56)
#define BK7258_AP_SWAP_GATE_MAGIC UINT32_C(0x47415445) /* "GATE" */
void bk7258_ap_sched_gate_clear(void);
void bk7258_ap_sched_gate_set(void);
bool bk7258_ap_sched_gate_test(void);

#define BK7258_AP_DBG_MARK_RESET_DONE       0x0101u
#define BK7258_AP_DBG_MARK_BEFORE_NX        0x0102u
#define BK7258_AP_DBG_MARK_BOARD_EARLY      0x0103u
#define BK7258_AP_DBG_MARK_C2START_ENTRY    0x0200u
#define BK7258_AP_DBG_MARK_C2START_CTRL1    0x0201u
#define BK7258_AP_DBG_MARK_C2START_RELEASED 0x0202u
#define BK7258_AP_DBG_MARK_C2START_HANDSHAKE_OK 0x0203u
#define BK7258_AP_DBG_MARK_CPU2_ENTRY       0x0300u
#define BK7258_AP_DBG_MARK_CPU2_GATE_WAIT   0x0304u
#define BK7258_AP_DBG_MARK_CPU2_GATE_OK     0x0305u
#define BK7258_AP_DBG_MARK_CPU2_GATE_TIMEOUT 0x0306u
#define BK7258_AP_DBG_MARK_CPU2_DISCARDED   0x0301u
#define BK7258_AP_DBG_MARK_CPU2_IRQ_ARMED   0x0302u
#define BK7258_AP_DBG_MARK_CPU2_READY       0x0303u

/* v5c bringup probes (AP side, word 40).  0x0320..0x0327 sit in the unused
 * gap between the CPU2 boot marks (0x0300-0x0306) and the SMP probe marks
 * (0x0f01-0x0f10).  The CP monitor prints every word-40 change.  The flush
 * mark discriminates the v5c gate-flush path: after CPU0 opens the gate it
 * polls word 40 until CPU2_READY (Mailbox armed), re-sends the SMP sched
 * kick that the gate window swallowed, and marks 0x0321 (or 0x0320 on a
 * bounded-flush timeout).  Then 0x0322..0x0327 follow the amp-init kthread:
 * entry, rptun ok/fail, mb_ipc ok/fail, and after the scheduler-running
 * record write. */

#define BK7258_AP_DBG_MARK_AP_FLUSH_TIMEOUT 0x0320u
#define BK7258_AP_DBG_MARK_AP_FLUSH_DONE    0x0321u
#define BK7258_AP_DBG_MARK_AMP_ENTRY        0x0322u
#define BK7258_AP_DBG_MARK_AMP_RPTUN_OK     0x0323u
#define BK7258_AP_DBG_MARK_AMP_RPTUN_FAIL   0x0324u
#define BK7258_AP_DBG_MARK_AMP_MBIPC_OK     0x0325u
#define BK7258_AP_DBG_MARK_AMP_MBIPC_FAIL   0x0326u
#define BK7258_AP_DBG_MARK_AMP_STAGE_MARK   0x0327u

/* v5d mb_ipc_init internal probes (first-call-only; see mb_ipc.c).
 * 0x032a = entered; 0x032b = route table open/ctrl done; 0x032c = about
 * to return OK.  mb_ipc.c references these values directly. */
#define BK7258_AP_DBG_MARK_AMP_MBIPC_INIT_ENTER 0x032au
#define BK7258_AP_DBG_MARK_AMP_MBIPC_ROUTES_OK  0x032bu
#define BK7258_AP_DBG_MARK_AMP_MBIPC_SOCK_READY 0x032cu

/* Cross-core exclusive-monitor probe marks (bring-up diagnostics).
 * CPU2 writes C2DONE after its half of the probe; CPU1 later overwrites
 * word 40 with PASS or FAIL (upper 16 bits = atomic fetch_add lost-update
 * count, 0 on a healthy part), then FAILSAFE once the single-core fallback
 * has parked CPU2.  The CP monitor prints every change, so a single cold
 * boot answers whether the SRAM global exclusive monitor arbitrates the
 * two AP cores the runtime scheduler CAS needs. */

#define BK7258_AP_DBG_MARK_SMP_PROBE_C2DONE  0x0f10u
#define BK7258_AP_DBG_MARK_SMP_PROBE_PASS    0x0f01u
#define BK7258_AP_DBG_MARK_SMP_PROBE_FAIL    0x0f02u
#define BK7258_AP_DBG_MARK_SMP_PROBE_FAILSAFE 0x0f03u

void bk7258_ap_dbg_mark(uint32_t mark);
uint32_t bk7258_ap_dbg_read(void);

/* UART0 panic token, SWAP word 50.  UART0 belongs to the CP; the AP has no
 * console UART.  An AP panic takes the token and writes the FIFO directly
 * (its RPMsg console is dead with interrupts off), and the CP drops its own
 * output while the token is held so the two never interleave.  The AP
 * releases it at PANIC_KERNEL_FINAL, before RPTUN wakes the CP; the CP clears
 * it at boot, in bk7258_ap_record_prepare(), and after a timeout. */

#define BK7258_AP_SWAP_UART_WORD  UINT32_C(50)
#define BK7258_AP_UART_MAGIC      UINT32_C(0x41505554) /* "APUT" */

void bk7258_ap_uart_token_take(void);
void bk7258_ap_uart_token_release(void);
bool bk7258_ap_uart_token_held(void);

/* AP panic record.  The AP console is an RPMsg UART, so NuttX's own panic
 * dump never leaves CPU1 once interrupts are off.  The AP panic notifier
 * copies the essentials here and the CP monitor prints them.  It sits at
 * SWAP offset 0x100, behind every word listed in the map above.  magic is
 * cleared while the record is rewritten and stored last. */

#define BK7258_AP_PANIC_OFFSET    UINT32_C(0x100)
#define BK7258_AP_PANIC_MAGIC     UINT32_C(0x4150504e) /* "APPN" */
#define BK7258_AP_PANIC_NBT       16

struct bk7258_ap_panic_s
{
  uint32_t magic;
  uint32_t boot_sequence;
  uint32_t linenum;
  uint32_t pc;
  uint32_t lr;
  uint32_t sp;
  uint32_t nbt;
  uint32_t bt[BK7258_AP_PANIC_NBT];
  char     filename[64];
  char     task[24];
  char     msg[48];
};

/* AP side: stamps magic and the current boot sequence. */

void bk7258_ap_panic_write(struct bk7258_ap_panic_s *panic);

/* CP side: true only for a complete record of the given boot sequence. */

bool bk7258_ap_panic_read(struct bk7258_ap_panic_s *panic,
                          uint32_t boot_sequence);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_AP_BOOT_H */

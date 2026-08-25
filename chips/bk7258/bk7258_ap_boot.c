/****************************************************************************
 * chips/bk7258/bk7258_ap_boot.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "include/bk7258_ap_boot.h"
#include "include/bk7258_memorymap.h"

#define BK7258_AP_RECORD_WORDS \
  (sizeof(struct bk7258_ap_boot_record_s) / sizeof(uint32_t))
#define BK7258_AP_RECORD_READ_ATTEMPTS 3
#define BK7258_AP_GENERATION_MAGIC UINT32_C(0x4150474e) /* "APGN" */
#define BK7258_AP_GENERATION_SLOTS 2

struct bk7258_ap_generation_s
{
  uint32_t magic;
  uint32_t sequence;
  uint32_t inverse;
  uint32_t checksum;
};

static_assert(sizeof(struct bk7258_ap_boot_record_s) == 64,
              "BK7258 AP boot record ABI must remain 64 bytes");
static_assert(sizeof(struct bk7258_ap_boot_record_s) +
              sizeof(struct bk7258_ap_generation_s) *
                BK7258_AP_GENERATION_SLOTS <= BK7258_SWAP_SIZE,
              "BK7258 AP records exceed the locked SWAP region");

static volatile struct bk7258_ap_boot_record_s *const g_ap_record =
  (volatile struct bk7258_ap_boot_record_s *)BK7258_SWAP_BASE;
static volatile struct bk7258_ap_generation_s *const g_ap_generation =
  (volatile struct bk7258_ap_generation_s *)
    (BK7258_SWAP_BASE + sizeof(struct bk7258_ap_boot_record_s));

static inline void bk7258_ap_barrier(void)
{
  __asm__ volatile ("dmb" : : : "memory");
}

static uint32_t bk7258_ap_checksum(
  const struct bk7258_ap_boot_record_s *record)
{
  const uint32_t *words = (const uint32_t *)record;
  uint32_t checksum = UINT32_C(0x7258a5a5);
  size_t index;

  for (index = 1; index < BK7258_AP_RECORD_WORDS - 1; index++)
    {
      checksum = (checksum << 5) | (checksum >> 27);
      checksum ^= words[index];
    }

  return checksum;
}

static uint32_t bk7258_ap_generation_checksum(uint32_t sequence)
{
  uint32_t rotated = (sequence << 13) | (sequence >> 19);

  return BK7258_AP_GENERATION_MAGIC ^ sequence ^ rotated ^
         UINT32_C(0x7258474e);
}

static bool bk7258_ap_generation_valid(
  const volatile struct bk7258_ap_generation_s *slot)
{
  uint32_t sequence = slot->sequence;

  bk7258_ap_barrier();
  return slot->magic == BK7258_AP_GENERATION_MAGIC && sequence != 0 &&
         slot->inverse == ~sequence &&
         slot->checksum == bk7258_ap_generation_checksum(sequence);
}

static uint32_t bk7258_ap_recover_sequence(void)
{
  uint32_t sequence = 0;
  unsigned int index;

  bk7258_ap_barrier();
  for (index = 0; index < BK7258_AP_GENERATION_SLOTS; index++)
    {
      if (bk7258_ap_generation_valid(&g_ap_generation[index]) &&
          g_ap_generation[index].sequence > sequence)
        {
          sequence = g_ap_generation[index].sequence;
        }
    }

  return sequence;
}

static uint32_t bk7258_ap_next_sequence(void)
{
  volatile struct bk7258_ap_generation_s *slot;
  uint32_t sequence = bk7258_ap_recover_sequence();
  unsigned int index;

  if (sequence == UINT32_MAX)
    {
      /* Define the 32-bit wrap explicitly. CP owns SWAP while CPU1 is held in
       * reset, so invalidate both old generations before restarting at one.
       * A reset between invalidations cannot revive a stale slot because each
       * magic store is an atomic 32-bit write. */

      for (index = 0; index < BK7258_AP_GENERATION_SLOTS; index++)
        {
          g_ap_generation[index].magic = 0;
          bk7258_ap_barrier();
        }

      sequence = 1;
    }
  else
    {
      sequence++;
    }
  slot = &g_ap_generation[sequence & 1];

  /* Preserve the other valid slot until this one is fully committed. A reset
   * during any of the first three stores therefore still leaves one complete
   * previous generation available. */

  slot->magic = 0;
  bk7258_ap_barrier();
  slot->sequence = sequence;
  slot->inverse = ~sequence;
  slot->checksum = bk7258_ap_generation_checksum(sequence);
  bk7258_ap_barrier();
  slot->magic = BK7258_AP_GENERATION_MAGIC;
  bk7258_ap_barrier();
  return sequence;
}

static void bk7258_ap_write(struct bk7258_ap_boot_record_s *record)
{
  volatile uint32_t *destination = (volatile uint32_t *)g_ap_record;
  const uint32_t *source = (const uint32_t *)record;
  uint32_t commit = (g_ap_record->commit + 1) | 1;
  size_t index;

  destination[0] = commit;
  bk7258_ap_barrier();

  record->checksum = bk7258_ap_checksum(record);
  for (index = 1; index < BK7258_AP_RECORD_WORDS; index++)
    {
      destination[index] = source[index];
    }

  bk7258_ap_barrier();
  destination[0] = commit + 1;
  bk7258_ap_barrier();
}

bool bk7258_ap_record_read(struct bk7258_ap_boot_record_s *record)
{
  const volatile uint32_t *source = (const volatile uint32_t *)g_ap_record;
  uint32_t *destination = (uint32_t *)record;
  uint32_t first;
  uint32_t last;
  unsigned int attempt;
  size_t index;

  for (attempt = 0; attempt < BK7258_AP_RECORD_READ_ATTEMPTS; attempt++)
    {
      first = source[0];
      if ((first & 1) != 0)
        {
          __asm__ volatile ("nop");
          continue;
        }

      /* Acquire the payload after observing an even commit value. */

      bk7258_ap_barrier();

      for (index = 0; index < BK7258_AP_RECORD_WORDS; index++)
        {
          destination[index] = source[index];
        }

      bk7258_ap_barrier();
      last = source[0];
      if (first != last || (last & 1) != 0)
        {
          continue;
        }

      return record->magic == BK7258_AP_BOOT_MAGIC &&
             record->abi_version == BK7258_AP_BOOT_ABI_VERSION &&
             record->size == sizeof(*record) &&
             record->checksum == bk7258_ap_checksum(record);
    }

  return false;
}

uint32_t bk7258_ap_record_prepare(void)
{
  struct bk7258_ap_boot_record_s record = {0};

  record.boot_sequence = bk7258_ap_next_sequence();
  record.magic = BK7258_AP_BOOT_MAGIC;
  record.abi_version = BK7258_AP_BOOT_ABI_VERSION;
  record.size = sizeof(record);
  record.stage = BK7258_AP_STAGE_CP_PREPARED;
  record.vector_base = BK7258_AP_XIP_VECTOR_BASE;
  record.build_id = BK7258_AP_BUILD_ID;
  bk7258_ap_write(&record);
  return record.boot_sequence;
}

static bool bk7258_ap_current(struct bk7258_ap_boot_record_s *record)
{
  if (!bk7258_ap_record_read(record))
    {
      *record = (struct bk7258_ap_boot_record_s){0};
      record->magic = BK7258_AP_BOOT_MAGIC;
      record->abi_version = BK7258_AP_BOOT_ABI_VERSION;
      record->size = sizeof(*record);
      record->boot_sequence = bk7258_ap_recover_sequence();
      record->build_id = BK7258_AP_BUILD_ID;
      return false;
    }

  return true;
}

void bk7258_ap_record_mark_reset(uint32_t vector_base, uint32_t reset_pc)
{
  struct bk7258_ap_boot_record_s record;

  bk7258_ap_current(&record);
  if (record.boot_sequence == 0)
    {
      return;
    }

  /* A reset observed while the record still describes a running AP was
   * initiated by AP itself (or by hardware), rather than by CP's
   * board_start_cpu() transaction.  Allocate a new generation so CP can
   * distinguish an AP-only reboot from an ordinary stage transition without
   * depending on the RPMsg transport that is about to disappear.  CP writes
   * CP_PREPARED before releasing CPU1, so CP-managed resets retain the
   * sequence prepared by the owner. */

  if (record.stage == BK7258_AP_STAGE_SCHEDULER_RUNNING)
    {
      record.boot_sequence = bk7258_ap_next_sequence();
      record.stage = BK7258_AP_STAGE_CP_PREPARED;
    }

  record.stage = BK7258_AP_STAGE_RESET_ENTERED;
  record.heartbeat = 0;
  record.fault = 0;
  record.vector_base = vector_base;
  record.reset_pc = reset_pc;
  bk7258_ap_write(&record);
}

void bk7258_ap_record_mark_scheduler(void)
{
  struct bk7258_ap_boot_record_s record;

  bk7258_ap_current(&record);
  if (record.boot_sequence == 0)
    {
      return;
    }
  record.stage = BK7258_AP_STAGE_SCHEDULER_RUNNING;
  bk7258_ap_write(&record);
}

void bk7258_ap_record_heartbeat(void)
{
  struct bk7258_ap_boot_record_s record;

  bk7258_ap_current(&record);
  if (record.boot_sequence == 0)
    {
      return;
    }
  record.stage = BK7258_AP_STAGE_SCHEDULER_RUNNING;
  record.heartbeat++;
  bk7258_ap_write(&record);
}

void bk7258_ap_record_fault(uint32_t fault)
{
  struct bk7258_ap_boot_record_s record;

  bk7258_ap_current(&record);
  if (record.boot_sequence == 0)
    {
      return;
    }
  record.stage = BK7258_AP_STAGE_FAULT;
  record.fault = fault;
  bk7258_ap_write(&record);
}

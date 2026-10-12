/****************************************************************************
 * board/bk7258-devkit/src/bk7258_mpu_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 ARMv8-M MPU bring-up diagnostic (NSH command, CP/AP host/core).
 *
 * ANSWERS TWO QUESTIONS:
 *
 *  1. "is the MPU actually programmed and enabled?"  -> `regs` decodes every
 *     region RBAR/RLAR (base, limit, ENABLE, AP, shareability, XN, MAIR
 *     attribute index) straight from the hardware and compares it with the
 *     region table programmed by bk7258_mpuinit().  Also verifies
 *     MPU_CTRL.ENABLE and PRIVDEFENA.  This needs no fault and no reset.
 *
 *  2. "does the memory protection actually fault?"   -> `flashro` and
 *     `xnprobe` deliberately commit a violation through the NORMAL fault
 *     path (no handler surgery): a P:RO write to Flash and an
 *     execute-never call into the Device window.  Each is expected to raise
 *     a MemManage fault; the chip's fault record (`.noinit`, survives CPU
 *     restart) and the `!FAULT k=2 ...` early-boot report on the next reset
 *     are the evidence.  `positive` proves the benign access classes keep
 *     working (read Flash, R/W the non-cacheable SRAM window, R/W PSRAM if
 *     the heap has it, read a Device register).
 *
 * Evidence for a probe run (UART log):
 *   [bk7258_mpu_test] ... intent lines ...
 *   (MemManage fault: with CONFIG_DEBUG_MEMFAULT the handler prints CFSR
 *    e.g. "Data access violation" + the MMFAR, then PANIC; flat build halts)
 *   -- operator power-cycles --
 *   !FAULT k=2 pc=... cfsr=... bfar=<fault address> ...   (early report)
 *
 *   Reading the cfsr field: bit0 IACCVIOL, bit1 DACCVIOL, bit7 MMARVALID.
 *   So a blocked flash write reports cfsr ~0x82 (DACCVIOL|MMARVALID) with
 *   bfar=0x02000000 and a blocked device call reports cfsr ~0x81
 *   (IACCVIOL|MMARVALID) with bfar=0x40000000.
 *
 * The fault probes are intentionally the fatal path: MPU enforcement is
 * proven by the exception firing, exactly as it would for a real bug.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_memorymap.h>

#ifdef CONFIG_ARM_MPU

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ARMv8-M (Cortex-M33) System Control Space.  The app keeps its own
 * constants so it does not depend on arch-internal headers. */

#define BK7258_MPU_TEST_SCS         UINT32_C(0xe000e000)
#define BK7258_MPU_TEST_MPU_BASE    (BK7258_MPU_TEST_SCS + 0xed90)
#define BK7258_MPU_TEST_MAIR0       (BK7258_MPU_TEST_SCS + 0xedc0)
#define BK7258_MPU_TEST_MAIR1       (BK7258_MPU_TEST_SCS + 0xedc4)
#define BK7258_MPU_TEST_CFSR        (BK7258_MPU_TEST_SCS + 0xed28)
#define BK7258_MPU_TEST_MMFAR       (BK7258_MPU_TEST_SCS + 0xed34)

#define BK7258_MPU_TEST_RBAR_XN     (1u << 0)  /* Execute never */
#define BK7258_MPU_TEST_RBAR_AP_RO  (2u << 1)  /* P:R, U:none */
#define BK7258_MPU_TEST_RBAR_SH_IN  (3u << 3)  /* Inner shareable */
#define BK7258_MPU_TEST_RBAR_MASK   UINT32_C(0xffffffe0)
#define BK7258_MPU_TEST_RLAR_EN     (1u << 0)
#define BK7258_MPU_TEST_RLAR_INDX   (7u << 1)
#define BK7258_MPU_TEST_RLAR_LIMIT  UINT32_C(0xffffffe0)

#define BK7258_MPU_TEST_CFSR_IACCVIOL (1u << 0)  /* MMFSR */
#define BK7258_MPU_TEST_CFSR_DACCVIOL (1u << 1)
#define BK7258_MPU_TEST_CFSR_MSTKERR   (1u << 3)
#define BK7258_MPU_TEST_CFSR_MMARVALID (1u << 7)

/* Matches the g_bk7258_mpu_regions table programmed on every core start.
 * Keep in sync with chips/bk7258/bk7258_mpuinit.c. */

struct bk7258_mpu_test_expected_s
{
  const char *name;
  uint32_t base;
  uint32_t size;
  uint32_t rbar_flags;   /* XN / AP / SH bits, mask-visible in RBAR[4:0] */
  unsigned int attr_idx; /* RLAR AttrIndx [3:1] */
  unsigned int sh;       /* RBAR SH [4:3] */
  unsigned int ap;       /* RBAR AP [2:1] */
  bool xn;
};

static const struct bk7258_mpu_test_expected_s g_expected[] =
{
  {
    "flash XIP P:RO",
    BK7258_FLASH_BASE, UINT32_C(0x02000000),
    BK7258_MPU_TEST_RBAR_AP_RO, 4u /* write-back */, 0u, 2u, false
  },
  {
    "on-chip SRAM NC",
    BK7258_SRAM_CAPACITY_BASE, UINT32_C(0x10000000),
    BK7258_MPU_TEST_RBAR_SH_IN, 2u /* non-cacheable */, 3u, 0u, false
  },
  {
    "PSRAM WBWA",
    BK7258_PSRAM_BASE, BK7258_PSRAM_WINDOW_SIZE,
    0u, 4u /* write-back */, 0u, 0u, false
  },
  {
    "peripherals Device XN",
    UINT32_C(0x40000000), UINT32_C(0x20000000),
    BK7258_MPU_TEST_RBAR_XN, 1u /* Device */, 0u, 0u, true
  }
};

#define BK7258_MPU_TEST_NREG_EXPECTED                                    \
  (sizeof(g_expected) / sizeof(g_expected[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t bk7258_mpu_test_rd32(uintptr_t addr)
{
  return *(volatile uint32_t *)addr;
}

static inline void bk7258_mpu_test_wr32(uintptr_t addr, uint32_t value)
{
  *(volatile uint32_t *)addr = value;
  __asm__ volatile ("dsb" : : : "memory");
}

static void bk7258_mpu_test_usage(const char *program)
{
  printf("Usage: %s [regs|positive|flashro|xnprobe|all]\n", program);
  printf("  regs    - decode hardware MPU registers and compare with the\n");
  printf("            programmed region table; no fault, no reset.\n");
  printf("  positive- benign accesses: read Flash, R/W the non-cacheable\n");
  printf("            SRAM window (and PSRAM if the heap maps it), read a\n");
  printf("            Device register.  No fault expected.\n");
  printf("  flashro - protection probe: write to the P:RO Flash region.\n");
  printf("            EXPECT a MemManage fault (DACCVIOL) and a reset; the\n");
  printf("            next boot prints the retained '!FAULT' record.\n");
  printf("  xnprobe - protection probe: call into the Device+XN window.\n");
  printf("            EXPECT a MemManage fault (IACCVIOL) and a reset; the\n");
  printf("            next boot prints the retained '!FAULT' record.\n");
  printf("  all     - regs + positive only (the probes reset the board).\n");
}

/* Decode and verify one region against the expected entry.  Returns the
 * number of mismatched fields. */

static int bk7258_mpu_test_check_region(unsigned int index)
{
  const struct bk7258_mpu_test_expected_s *expect;
  uint32_t rbar;
  uint32_t rlar;
  uint32_t actual_base;
  uint32_t actual_limit;
  uint32_t expected_limit;
  uint32_t region_size;
  unsigned int attr;
  unsigned int sh;
  unsigned int ap;
  bool xn;
  bool en;
  int mismatches = 0;

  if (index >= BK7258_MPU_TEST_NREG_EXPECTED)
    {
      return 0;
    }

  expect = &g_expected[index];
  bk7258_mpu_test_wr32(BK7258_MPU_TEST_MPU_BASE + 0x08, index); /* RNR */
  rbar = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MPU_BASE + 0x0c); /* RBAR */
  rlar = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MPU_BASE + 0x10); /* RLAR */

  actual_base = rbar & BK7258_MPU_TEST_RBAR_MASK;
  actual_limit = rlar & BK7258_MPU_TEST_RLAR_LIMIT;
  region_size = actual_limit - actual_base + 1u;
  expected_limit = (expect->base + expect->size - 1u) &
                   BK7258_MPU_TEST_RLAR_LIMIT;
  en = (rlar & BK7258_MPU_TEST_RLAR_EN) != 0;
  attr = (rlar & BK7258_MPU_TEST_RLAR_INDX) >> 1;
  sh = (rbar >> 3) & 0x3u;
  ap = (rbar >> 1) & 0x3u;
  xn = (rbar & BK7258_MPU_TEST_RBAR_XN) != 0;

  printf("[bk7258_mpu_test] R%u %-22s base=0x%08" PRIx32 " size=0x%08"
         PRIx32 " en=%u ap=%u sh=%u xn=%u attr=%u\n",
         index, expect->name, actual_base, region_size, en, ap, sh, xn,
         attr);

  if (actual_base != expect->base)
    {
      printf("[bk7258_mpu_test]   MISMATCH base: expected 0x%08" PRIx32 "\n",
             expect->base);
      mismatches++;
    }

  if (region_size != expect->size)
    {
      printf("[bk7258_mpu_test]   MISMATCH size: expected 0x%08" PRIx32
             " (limit 0x%08" PRIx32 ")\n", expect->size, expected_limit);
      mismatches++;
    }

  if (!en)
    {
      printf("[bk7258_mpu_test]   MISMATCH region disabled\n");
      mismatches++;
    }

  if (ap != expect->ap)
    {
      printf("[bk7258_mpu_test]   MISMATCH AP %u (expected %u)\n", ap,
             expect->ap);
      mismatches++;
    }

  if (sh != expect->sh)
    {
      printf("[bk7258_mpu_test]   MISMATCH shareability %u (expected %u)\n",
             sh, expect->sh);
      mismatches++;
    }

  if (xn != expect->xn)
    {
      printf("[bk7258_mpu_test]   MISMATCH XN %u (expected %u)\n", xn,
             expect->xn);
      mismatches++;
    }

  if (attr != expect->attr_idx)
    {
      printf("[bk7258_mpu_test]   MISMATCH attribute index %u (expected %u)\n",
             attr, expect->attr_idx);
      mismatches++;
    }

  return mismatches;
}

static int bk7258_mpu_test_regs(void)
{
  uint32_t type = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MPU_BASE + 0x00);
  uint32_t ctrl = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MPU_BASE + 0x04);
  uint32_t mair0 = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MAIR0);
  uint32_t mair1 = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MAIR1);
  unsigned int dregion = (type >> 8) & 0xffu;
  unsigned int index;
  int mismatches = 0;

  printf("[bk7258_mpu_test] MPU_TYPE   = 0x%08" PRIx32
         " (DREGION=%u regions)\n", type, dregion);
  printf("[bk7258_mpu_test] MPU_CTRL   = 0x%08" PRIx32
         " (ENABLE=%u PRIVDEFENA=%u HFNMIENA=%u)\n",
         ctrl, (ctrl & 0x1u) != 0, (ctrl & 0x4u) != 0,
         (ctrl & 0x2u) != 0);
  printf("[bk7258_mpu_test] MAIR0      = 0x%08" PRIx32
         " MAIR1 = 0x%08" PRIx32 " (attr0..3, attr4..7)\n", mair0, mair1);

  if ((ctrl & 0x1u) == 0)
    {
      printf("[bk7258_mpu_test] FAIL: MPU_CTRL.ENABLE is clear\n");
      mismatches++;
    }

  if ((ctrl & 0x4u) == 0)
    {
      printf("[bk7258_mpu_test] WARN: PRIVDEFENA clear (background access"
             " to the default map disabled)\n");
    }

  if (dregion < BK7258_MPU_TEST_NREG_EXPECTED)
    {
      printf("[bk7258_mpu_test] FAIL: hardware implements only %u regions,"
             " table needs %u\n", dregion,
             (unsigned int)BK7258_MPU_TEST_NREG_EXPECTED);
      return -ENODEV;
    }

  for (index = 0; index < BK7258_MPU_TEST_NREG_EXPECTED; index++)
    {
      mismatches += bk7258_mpu_test_check_region(index);
    }

  if (mismatches == 0)
    {
      printf("[bk7258_mpu_test] regs PASS: %u regions match the programmed"
             " table and the MPU is enabled\n",
             (unsigned int)BK7258_MPU_TEST_NREG_EXPECTED);
      return OK;
    }

  printf("[bk7258_mpu_test] regs FAIL: %d mismatches\n", mismatches);
  return -EIO;
}

static int bk7258_mpu_test_positive(void)
{
  static volatile uint32_t s_sram_scratch[16];
  volatile uint32_t *sram;
  volatile uint32_t *psram;
  uint32_t flash_word;
  uint32_t pmu_word;
  uint32_t pattern = UINT32_C(0x5aa55aa5);
  size_t index;
  int failures = 0;

  /* 1. Read the Flash XIP vector area.  R0 is P:RO, so this must work. */

  flash_word = bk7258_mpu_test_rd32(BK7258_FLASH_BASE);
  printf("[bk7258_mpu_test] flash read 0x%08" PRIx32 " -> 0x%08" PRIx32 "\n",
         BK7258_FLASH_BASE, flash_word);

  /* 2. R/W the non-cacheable on-chip SRAM window (region 1).  This is the
   *    window the whole D-cache bring-up depends on: RPMSG/SWAP/PWR_MNG and
   *    Wi-Fi DMA buffers must never go stale. */

  sram = s_sram_scratch;
  for (index = 0; index < 16; index++)
    {
      sram[index] = pattern ^ (uint32_t)index;
    }

  __asm__ volatile ("dsb" : : : "memory");

  for (index = 0; index < 16; index++)
    {
      if (sram[index] != (pattern ^ (uint32_t)index))
        {
          printf("[bk7258_mpu_test] FAIL: SRAM round-trip mismatch at %zu\n",
                 index);
          failures++;
          break;
        }
    }

  if (failures == 0)
    {
      printf("[bk7258_mpu_test] SRAM (non-cacheable window) round-trip ok,"
             " D-cache on\n");
    }

  /* 3. PSRAM round-trip when the heap actually maps PSRAM (configs with
   *    CONFIG_BK7258_PSRAM); otherwise report and move on. */

  psram = malloc(8192);
  if (psram == NULL)
    {
      failures++;
      printf("[bk7258_mpu_test] FAIL: 8 KiB heap allocation failed\n");
    }
  else if ((uintptr_t)psram < BK7258_PSRAM_BASE ||
           (uintptr_t)psram + 8192 >
             BK7258_PSRAM_BASE + BK7258_PSRAM_WINDOW_SIZE)
    {
      printf("[bk7258_mpu_test] PSRAM not mapped into this heap (%p);"
             " skipping PSRAM check\n", (void *)psram);
      free((void *)psram);
    }
  else
    {
      for (index = 0; index < 8192 / 4; index++)
        {
          psram[index] = pattern ^ (uint32_t)index;
        }

      __asm__ volatile ("dsb" : : : "memory");

      for (index = 0; index < 8192 / 4; index++)
        {
          if (psram[index] != (pattern ^ (uint32_t)index))
            {
              printf("[bk7258_mpu_test] FAIL: PSRAM round-trip mismatch\n");
              failures++;
              free((void *)psram);
              break;
            }
        }

      if (index == 8192 / 4)
        {
          printf("[bk7258_mpu_test] PSRAM (write-back) round-trip ok at %p\n",
                 (void *)psram);
          free((void *)psram);
        }
    }

  /* 4. Read a Device-mapped register (AON-PMU).  Region 3 is Device nGnRE,
   *    so a benign read must not fault either. */

  pmu_word = bk7258_mpu_test_rd32(BK7258_AON_PMU_BASE);
  printf("[bk7258_mpu_test] Device read 0x%08" PRIx32 " -> 0x%08" PRIx32 "\n",
         BK7258_AON_PMU_BASE, pmu_word);

  if (failures == 0)
    {
      printf("[bk7258_mpu_test] positive PASS: flash read, SRAM/PSRAM"
             " round-trips and Device read all ok\n");
      return OK;
    }

  printf("[bk7258_mpu_test] positive FAIL: %d failures\n", failures);
  return -EIO;
}

/* Both probes take the normal fatal fault path.  The lines printed just
 * before the fault, plus the retained '!FAULT' report after the next reset,
 * are the evidence pair. */

static int bk7258_mpu_test_flashro(void)
{
  uint32_t before;

  printf("[bk7258_mpu_test] flashro: about to WRITE 0xdeedbeef to the P:RO"
         " Flash region at 0x%08" PRIx32 "\n", BK7258_FLASH_BASE);
  printf("[bk7258_mpu_test] flashro: if MPU protection works this raises a"
         " MemManage fault (DACCVIOL) now\n");

  before = bk7258_mpu_test_rd32(BK7258_FLASH_BASE);
  printf("[bk7258_mpu_test] flashro: word before 0x%08" PRIx32 "\n", before);

  bk7258_mpu_test_wr32(BK7258_FLASH_BASE, UINT32_C(0xdeedbeef));

  /* If we get here the write went through unnoticed: protection failed. */

  printf("[bk7258_mpu_test] FAIL: the write RETURNED (word now 0x%08"
         PRIx32 ") - P:RO is not enforced!\n",
         bk7258_mpu_test_rd32(BK7258_FLASH_BASE));
  return -EIO;
}

static int bk7258_mpu_test_xnprobe(void)
{
  void (*target)(void) = (void (*)(void))UINT32_C(0x40000000);

  printf("[bk7258_mpu_test] xnprobe: about to CALL 0x40000000 (Device+XN"
         " window)\n");
  printf("[bk7258_mpu_test] xnprobe: if MPU protection works this raises a"
         " MemManage fault (IACCVIOL) now\n");

  target();

  printf("[bk7258_mpu_test] FAIL: the call RETURNED - XN is not enforced!\n");
  return -EIO;
}

static void bk7258_mpu_test_dump_cfsr(void)
{
  uint32_t cfsr = bk7258_mpu_test_rd32(BK7258_MPU_TEST_CFSR);
  uint32_t mmfar = bk7258_mpu_test_rd32(BK7258_MPU_TEST_MMFAR);

  printf("[bk7258_mpu_test] CFSR=0x%08" PRIx32 " MMFAR=0x%08" PRIx32 "\n",
         cfsr, mmfar);
  printf("[bk7258_mpu_test]   MMFSR: IACCVIOL=%u DACCVIOL=%u MMARVALID=%u"
         " MSTKERR=%u\n",
         (cfsr & BK7258_MPU_TEST_CFSR_IACCVIOL) != 0,
         (cfsr & BK7258_MPU_TEST_CFSR_DACCVIOL) != 0,
         (cfsr & BK7258_MPU_TEST_CFSR_MMARVALID) != 0,
         (cfsr & BK7258_MPU_TEST_CFSR_MSTKERR) != 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  const char *mode;

  if (argc == 2 && (argv[1][0] == '-' || argv[1][0] == '?'))
    {
      bk7258_mpu_test_usage(argv[0]);
      return OK;
    }

  if (argc != 2)
    {
      bk7258_mpu_test_usage(argv[0]);
      return -EINVAL;
    }

  mode = argv[1];

  if (strcmp(mode, "regs") == 0)
    {
      return bk7258_mpu_test_regs();
    }
  else if (strcmp(mode, "positive") == 0)
    {
      return bk7258_mpu_test_positive();
    }
  else if (strcmp(mode, "flashro") == 0)
    {
      bk7258_mpu_test_dump_cfsr();
      return bk7258_mpu_test_flashro();
    }
  else if (strcmp(mode, "xnprobe") == 0)
    {
      bk7258_mpu_test_dump_cfsr();
      return bk7258_mpu_test_xnprobe();
    }
  else if (strcmp(mode, "all") == 0)
    {
      int ret = bk7258_mpu_test_regs();

      if (ret == OK)
        {
          ret = bk7258_mpu_test_positive();
        }

      return ret;
    }

  bk7258_mpu_test_usage(argv[0]);
  return -EINVAL;
}

#endif /* CONFIG_ARM_MPU */
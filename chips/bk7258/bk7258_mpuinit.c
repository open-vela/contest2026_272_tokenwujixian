/****************************************************************************
 * chips/bk7258/bk7258_mpuinit.c
 *
 * ARMv8-M (Cortex-M33) MPU bring-up for BK7258.
 *
 * The BK7258 has an MPU, not an MMU (ARMv8-M MPU with unified instruction
 * and data regions).  This file programs the kernel region table and enables
 * the MPU.  It runs on every physical core (CPU0 CP __start, CPU1 and CPU2
 * AP __start paths) once .data/.bss are valid and the early fault handlers
 * are attached, so any MPU setup mistake produces a MemManage fault dump
 * instead of a silent hang.
 *
 * The attribute split -- not a permission split -- is the point:
 *
 *   1. Flash XIP (0x02000000) and PSRAM (0x60000000) are Normal write-back,
 *      i.e. cacheable once CONFIG_ARMV8M_DCACHE is enabled.
 *   2. On-chip SRAM (0x28000000) is Normal non-cacheable.  The whole SRAM
 *      window holds RPMSG_SHM, SWAP, PWR_MNG, the AP spinlock and the Wi-Fi
 *      pbufs that the MAC/PHY DMA engine writes without CPU cache
 *      coherency.  Marking it non-cacheable is what lets D-cache actually
 *      run (the previous code had to disable D-cache globally to keep those
 *      windows coherent); it mirrors the authority (vendor) MPU config
 *      documented in wifi/BEHAVIOR_ALIGNMENT_AUDIT.md section 3.2.
 *   3. Peripheral space (0x40000000) is Device (nGnRE), never cached.
 *
 * PRIVDEFENA keeps privileged background access to the default memory map,
 * so addresses not listed above (PPB, aliases, holes) stay accessible with
 * default attributes -- this is a flat privileged build.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/barriers.h>

#include "arm_internal.h"
#include "mpu.h"

#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"

#ifdef CONFIG_ARM_MPU

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct mpu_region_s g_bk7258_mpu_regions[] =
{
  /* Flash XIP alias window (0x02000000, 32 MiB).  Both the CP image
   * (0x02010000) and the AP image (0x02160000) execute from here.  Normal
   * write-back, read-only, privileged. */

  {
    BK7258_FLASH_BASE,              UINT32_C(0x02000000),
    MPU_RBAR_AP_RONO,               MPU_RLAR_WRITE_BACK
  },

  /* On-chip SRAM (0x28000000), 256 MiB eigenspace.  The real window is
   * 640 KiB ending at 0x280a0000 and hosts the CP/AP shared regions
   * (RPMSG_SHM, SWAP, PWR_MNG, AP spinlock) plus every DMA-visible Wi-Fi
   * buffer.  Normal non-cacheable, inner shareable, privileged, so D-cache
   * can be enabled without breaking cross-core or MAC/PHY DMA traffic. */

  {
    BK7258_SRAM_CAPACITY_BASE,      UINT32_C(0x10000000),
    MPU_RBAR_AP_RWNO | MPU_RBAR_SH_INNER,
    MPU_RLAR_NONCACHEABLE
  },

  /* External PSRAM window (0x60000000, 64 MiB).  The controller maps up to
   * 64 MiB; the fitted-device size is discovered before any portion enters
   * the heap.  Normal write-back, privileged. */

  {
    BK7258_PSRAM_BASE,              BK7258_PSRAM_WINDOW_SIZE,
    MPU_RBAR_AP_RWNO,               MPU_RLAR_WRITE_BACK
  },

  /* Peripheral address space (0x40000000, 512 MiB): SYS, AON, UART, TIMER,
   * Mailbox, GDMA, SADC, TRNG, OTP, PSRAM controller, ...  Device nGnRE,
   * privileged, never cached.  Execute-never on top of Device: no code ever
   * lives in this window, so an XN violation (IACCVIOL) is a hard
   * out-of-band memory-protection signal for the bk7258_mpu_test probe. */

  {
    UINT32_C(0x40000000),           UINT32_C(0x20000000),
    MPU_RBAR_AP_RWNO | MPU_RBAR_XN, MPU_RLAR_DEVICE
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Reset the MPU to disabled and invalidate every region the hardware
 * implements.  The Bootloader and the vendor AP lifecycle may have left the
 * MPU enabled with opaque regions over shared SRAM; starting clean makes the
 * table below deterministic on every core.  This mirrors mpu_reset_internal()
 * but is kept here so the chip bring-up never depends on CONFIG_ARM_MPU_RESET
 * being enabled by the board config. */

static void bk7258_mpu_reset(void)
{
  uint32_t region;
  uint32_t regions;

  regions = (getreg32(MPU_TYPE) & MPU_TYPE_DREGION_MASK) >>
            MPU_TYPE_DREGION_SHIFT;

  for (region = 0; region < regions; region++)
    {
      putreg32(region, MPU_RNR);
      putreg32(0, MPU_RLAR);
      putreg32(0, MPU_RBAR);
    }

  putreg32(0, MPU_CTRL);

  UP_MB();
}

/* Program the region table with fixed region numbers 0..n.  Direct register
 * writes are used instead of mpu_configure_region()/mpu_modify_region() so a
 * secondary SMP core does not advance the shared g_mpu_region allocator:
 * every core ends up with the identical config at regions 0..n.  The table
 * is clamped to the number of data regions the integrated MPU actually
 * implements (MPU_TYPE.DREGION). */

static void bk7258_mpu_configure(void)
{
  const struct mpu_region_s *conf;
  size_t count;
  size_t index;
  uint32_t regions;
  uintptr_t limit;
  uintptr_t rbase;

  regions = (getreg32(MPU_TYPE) & MPU_TYPE_DREGION_MASK) >>
            MPU_TYPE_DREGION_SHIFT;

  count = sizeof(g_bk7258_mpu_regions) / sizeof(g_bk7258_mpu_regions[0]);
  if (count > regions)
    {
      count = regions;
    }

  for (index = 0; index < count; index++)
    {
      conf = &g_bk7258_mpu_regions[index];

      /* Regions must be power-of-two aligned; RBAR.ADDR and RLAR.LIMIT
       * mask to bit 5, and the indexes below are all aligned. */

      rbase = conf->base & MPU_RBAR_BASE_MASK;
      limit = (conf->base + conf->size - 1) & MPU_RLAR_LIMIT_MASK;

      putreg32(index, MPU_RNR);
      putreg32(rbase | conf->flags1, MPU_RBAR);
      putreg32(limit | conf->flags2 | MPU_RLAR_ENABLE, MPU_RLAR);

      UP_MB();
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_mpuinit(void)
{
  bk7258_mpu_reset();
  bk7258_mpu_configure();

  /* mpu_control() programs MAIR0/MAIR1 (the attribute encodings the RLAR
   * indexes reference) and enables the MPU with PRIVDEFENA so uncommon
   * addresses keep working under the privileged default map. */

  mpu_control(true, false, true);
}

#endif /* CONFIG_ARM_MPU */
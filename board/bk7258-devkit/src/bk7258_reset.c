/****************************************************************************
 * board/bk7258-devkit/src/bk7258_reset.c
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_RESET

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <sys/boardctl.h>

/* Chip-level reset for BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER.
 *
 * The generic SYSRESETREQ path below only resets the CPU0 core: on this SoC
 * the warm reset vector re-enters the startup trampoline directly, so the
 * BootROM/BL2 chain never runs and the serial download handshake window
 * never opens.  "reboot bootloader" must reach the BL2 download listen
 * window instead, so it takes the vendor WDT path: a real chip-level reset
 * from the Always-On PMU, equivalent to the reset button.
 *
 * Every register fact below is lifted from the vendor Armino SDK (BK7258,
 * secure address map, SOC_ADDR_OFFSET == 0):
 *
 * - soc/bk7258/reg_base.h:
 *     SOC_AON_PMU_REG_BASE 0x44000000, SOC_AON_WDT_REG_BASE 0x44000600
 * - soc/bk7258_ap/soc/aon_pmu_reg.h:
 *     AON_PMU_R2_ADDR  = PMU base + 0x2 << 2
 *     AON_PMU_R41_ADDR = PMU base + 0x41 << 2
 * - driver/reset_reason/reset_reason.h (BK7236XX):
 *     REBOOT_TAG_ADDR 0x20003FF8 (free DTCM slot, REBOOT_TAG_REQ
 *     0xAA55AA55).  A set tag tells the BootROM to warm fast-jump into the
 *     previous app; a cleared tag makes any reset run the full cold chain
 *     (BootROM -> BL2).  The vendor's own planned reboots set the tag
 *     before arming the WDT (wdt_hal_nmi_reboot); deliberately clearing it
 *     turns the same WDT reset into the recover-from-hung-firmware cold
 *     path that the BootROM already implements.
 * - driver/reset_reason/reset_reason.c + include/components/system.h:
 *     RESET_SOURCE_WATCHDOG == 0x2.  The AON PMU survives the chip reset,
 *     so the reason recorded there is what the ROM/BL2 reads to classify
 *     the next boot: CP reason in PMU R0 bits[4:11], AP reason in
 *     bits[24:30].  Recording WATCHDOG here is what makes the boot cold.
 * - pmu/aon_pmu_driver.c + soc/bk7258_ap/hal/aon_pmu_hal.c:
 *     aon_pmu_drv_wdt_change_not_rosc_clk(): PMU R41 bits[1:0] = 0, selects
 *     clk_DIVD as the WDT LPO source instead of ROSC.
 *     aon_pmu_hal_wdt_rst_dev_enable(): PMU R2 bits[5:0] = 0x26, allows the
 *     AON WDT to reset the device at the PMU level.
 * - soc/common/hal/wdt_hal.c wdt_hal_force_reboot():
 *     arm the AON WDT with the mandatory two-write key sequence
 *     0x5A000A / 0xA5000A (key 0x5A then 0xA5 in bits[23:16], emergency
 *     period 0xA).
 *
 * The CP firmware runs from the 0x280xxxxx SRAM window, so the DTCM tag
 * slot at 0x20003FF8 is vendor-reserved and safe to write.
 */

#define BK7258_AON_PMU_BASE      0x44000000ul
#define BK7258_AON_PMU_R0_ADDR   (BK7258_AON_PMU_BASE + (0x0 << 2))
#define BK7258_AON_PMU_R2_ADDR   (BK7258_AON_PMU_BASE + (0x2 << 2))
#define BK7258_AON_PMU_R41_ADDR  (BK7258_AON_PMU_BASE + (0x41 << 2))
#define BK7258_AON_WDT_BASE      0x44000600ul
#define BK7258_REBOOT_TAG_ADDR   0x20003ff8ul

/* The board library build does not see the arch-internal inline register
 * accessors, so use plain volatile pointer access here. */

static inline uint32_t bk7258_reg_rd(uintptr_t addr)
{
  return *(volatile uint32_t *)addr;
}

static inline void bk7258_reg_wr(uintptr_t addr, uint32_t value)
{
  *(volatile uint32_t *)addr = value;
}

#ifdef CONFIG_BK7258_COMPONENT_CP
/****************************************************************************
 * Name: bk7258_enter_bootloader_reset
 *
 * Description:
 *   Force a chip-level reset through the AON WDT with the vendor reboot
 *   tag cleared, so the BootROM runs the full cold chain and BL2 opens the
 *   serial download handshake window.  Mirrors wdt_hal_force_reboot() minus
 *   set_reboot_tag() and minus the NMI WDT arming (this port registers no
 *   NMI handler; the AON WDT alone resets the whole chip).
 *
 *   The RPTUN reboot notifier has already reset the AP by the time the
 *   reboot chain reaches board_reset(); this chip reset covers CPU1 again
 *   regardless, and the normal bring-up re-releases it afterwards.
 *
 ****************************************************************************/

static void bk7258_enter_bootloader_reset(void)
{
  uint32_t val;

  /* Clear the vendor warm fast-jump tag: with no tag the BootROM treats
   * the coming WDT reset like a recover-from-hung-firmware boot. */

  bk7258_reg_wr(BK7258_REBOOT_TAG_ADDR, 0ul);

  /* Vendor bk_misc_set_ap_reset_reason()/bk_misc_set_cp_reset_reason():
   * record RESET_SOURCE_WATCHDOG (0x2) as the reset reason.  The AON PMU
   * survives the chip reset, so this is what the BootROM/BL2 reads on the
   * next boot to classify it; the previous boot leaves a stale software
   * value there (this port never writes the field), and without this write
   * the ROM keeps skipping BL2 even though the tag is gone.  AP reason
   * lives in R0 bits[24:30] (7 bits), CP reason in bits[4:11] (8 bits). */

  val = bk7258_reg_rd(BK7258_AON_PMU_R0_ADDR);
  val &= ~((0x7ful << 24) | (0xfful << 4));
  val |= (0x2ul << 24) | (0x2ul << 4);
  bk7258_reg_wr(BK7258_AON_PMU_R0_ADDR, val);

  /* Vendor aon_pmu_drv_wdt_change_not_rosc_clk(): select clk_DIVD as the
   * WDT LPO source so the emergency period bites deterministically. */

  val = bk7258_reg_rd(BK7258_AON_PMU_R41_ADDR);
  val &= ~0x3ul;
  bk7258_reg_wr(BK7258_AON_PMU_R41_ADDR, val);

  /* Vendor aon_pmu_hal_wdt_rst_dev_enable(): gate the AON WDT through to
   * the PMU reset line, keeping every other R2 bit unchanged. */

  val = bk7258_reg_rd(BK7258_AON_PMU_R2_ADDR);
  val &= ~0x3ful;
  val |= 0x26ul;
  bk7258_reg_wr(BK7258_AON_PMU_R2_ADDR, val);

  /* Vendor wdt_hal_force_reboot(): arm the AON WDT.  The control word
   * carries the key in bits[23:16] and the period in the low bits; two
   * writes with different keys are required for the value to latch. */

  bk7258_reg_wr(BK7258_AON_WDT_BASE, 0x5a000aul);
  bk7258_reg_wr(BK7258_AON_WDT_BASE, 0xa5000aul);

  /* The reset comes from the PMU, not from here. */

  for (; ; );
}
#endif /* CONFIG_BK7258_COMPONENT_CP */

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reset the board.  Every status keeps the architecture-provided
 *   SYSRESETREQ path (the reset vector re-enters the normal BK7258 startup
 *   trampoline), except ENTER_BOOTLOADER on the CP, which takes the AON WDT
 *   chip-level reset so the BootROM/BL2 download window can be reached.
 *   This is not a CPU1/RPTUN lifecycle operation: the RPTUN reboot notifier
 *   runs earlier in the reboot chain and resets the AP independently of
 *   which physical reset is chosen here.
 *
 ****************************************************************************/

int board_reset(int status)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  if (status == (int)BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER)
    {
      bk7258_enter_bootloader_reset();
    }
#else
  (void)status;
#endif

  up_systemreset();
  return 0;
}

#endif /* CONFIG_BOARDCTL_RESET */

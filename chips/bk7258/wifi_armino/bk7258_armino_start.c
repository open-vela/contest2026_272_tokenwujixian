/****************************************************************************
 * chips/bk7258/wifi_armino/bk7258_armino_start.c
 *
 * Phase-one entry for the Armino iperf baseline (docs/compose/spec/
 * bk7258-wifi-native.md, S2.2).
 *
 * On Armino, Reset_Handler reaches entry_main()
 * (cp/components/bk_startup/system_main.c:412) on the main stack before any
 * scheduler exists. NuttX owns reset and the scheduler, so entry_main() runs
 * here in a kernel thread instead. Everything it calls is unchanged Armino
 * source; the FreeRTOS API underneath, including rtos_start_scheduler(), is
 * the T9 seam.
 ****************************************************************************/

#include <nuttx/config.h>

#include <malloc.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/signal.h>

#include "arm_internal.h"
#include "bk7258_memorymap.h"
#include "include/bk7258_clock.h"
#include "include/bk7258_dvfs.h"

#define BK7258_ARMINO_ENTRY_PRIORITY  85
#define BK7258_ARMINO_ENTRY_STACKSIZE 8192

/* How long entry_main() waits for NuttX to have powered CPU1 up. */

#define BK7258_ARMINO_AP_WAIT_MS      5000
#define BK7258_ARMINO_AP_POLL_US      10000

/* REBOOT_TAG_ADDR (cp/middleware/driver/reset_reason/reset_reason.h:28) is
 * 0x20003FF8 + SOC_ADDR_OFFSET, and SOC_ADDR_OFFSET is 0 because the
 * baseline sets CONFIG_SPE (cp/include/soc/bk7258/reg_base.h:22). The board
 * reset code uses the same slot (board/bk7258-devkit/src/bk7258_reset.c:64).
 */

#define BK7258_ARMINO_REBOOT_TAG_ADDR 0x20003ff8u

void entry_main(void);
int bk7258_armino_start(void);
void set_reboot_tag(uint32_t tag);

/* Two symbols of units NuttX replaces at reset:
 *
 * build_version comes from the baseline's generated link helper
 * build/.../project_elf_src.c, which is not an Armino component source.
 * Same definition; read by trap_base.c and at_server's _atsvr_func.c.
 */

volatile const char build_version[] = __DATE__ " " __TIME__;

/* set_reboot_tag() lives in the Armino reset entry
 * (cmsis/.../startup_bk7236.c:352), excluded because reset belongs to NuttX.
 * Its only caller outside that file is wdt_hal.c:94 (REBOOT_TAG_REQ before a
 * watchdog reboot); same body.
 */

void set_reboot_tag(uint32_t tag)
{
  putreg32(tag, BK7258_ARMINO_REBOOT_TAG_ADDR);
}

/* Prototypes from the baseline DWARF. */

void sys_drv_early_init(void);
void bk_wdt_force_feed(void);
void pm_hardware_init(void);
int bk_pm_cp1_auto_power_down_state_set(uint32_t state);
int bk_pm_mem_auto_power_down_state_set(uint32_t state);
void cm_backtrace_init(const char *firmware_name,
                       const char *hardware_ver,
                       const char *software_ver);
void bk7258_psram_addregion(void);
void bk7258_armino_early_initialize(void);

/* The Armino init that startup_bk7236.c runs before entry_main(), in its
 * order, called from board_early_initialize(): a heap exists there (the
 * Armino code this reaches can allocate) and the scheduler does not run yet,
 * as on Armino. Skipped because NuttX owns them: SystemInit() (VTOR, FPU and
 * SAU are set by __start), mpu_enable() (CONFIG_ARM_MPU is off) and the
 * DCache enable (__start keeps the DCache off).
 */

void bk7258_armino_early_initialize(void)
{
  /* Reset_Handler (startup_bk7236.c:394-413). */

  bk_wdt_force_feed();
  set_reboot_tag(0);

  /* sys_hal_early_init() rewrites the analog PLL/xtal/LDO registers, which
   * Armino does first thing after reset; here the NuttX console is already
   * running. 'a' and 'b' bracket it: a clean 'b' means the UART survived.
   */

  up_putc('a');
  sys_drv_early_init();
  up_putc('b');

  /* _start (startup_bk7236.c:414-441), also before entry_main() and before
   * any shell init there. Version strings are the baseline's FIREWARE_NAME,
   * HARDWARE_VERSION and SOFTWARE_VERSION.
   */

  cm_backtrace_init("app", "V1.0.0", "V1.0.0");
  pm_hardware_init();
  bk_pm_cp1_auto_power_down_state_set(0);  /* PM_CP1_AUTO_CTRL_DISABLE */
  bk_pm_mem_auto_power_down_state_set(0);  /* PM_MEM_AUTO_CTRL_DISABLE */

  /* Shared CPU/bus clock, Armino's highest step: CPU0 240 MHz, CPU1/CPU2
   * 480 MHz, BUS 240 MHz (PM_CPU_FRQ_480M).  Voted here because pm is up and
   * because physical CPU1 is released later, from the rptun thread -- the AP
   * must boot at its final frequency, not have it change under its feet.
   * The call logs the divider readback, so the step is provable from the
   * console without a scope.
   */

  (void)bk7258_clock_cpu_boost();

  /* Then apply the shared operating point ourselves, because the SDK's own
   * 480M row does not exist: sys_hal_switch_cpu_bus_freq() stops at 320M and
   * lets the 480M request fall through "default", which is why the vote alone
   * left the AP at half of CPU0.  This runs after the vote so the register
   * state is ours, and before CPU1 is released so CPU1 boots at its final
   * clock.
   */

  (void)bk7258_dvfs_set_opp(BK7258_OPP_480M);
  bk7258_dvfs_report();

  /* pm_hardware_init() has switched AHBP off, PSRAM included; PSRAM init
   * powers it back on before the region joins the heap, the same order as
   * Armino (startup powers AHBP off, bk_psram_init() votes PSRAM on).
   */

#if CONFIG_MM_REGIONS > 1
  bk7258_psram_addregion();
#endif
}

/* CPU1 has one owner, NuttX (docs/compose/spec/bk7258-wifi-native.md,
 * S2.10). Armino's own boot path, user_app_main() ->
 * bk_pm_module_vote_boot_cp1_ctrl(), starts CPU1 from its partition 1
 * address only while sys_drv_module_power_state_get(CPU1) reads OFF, which
 * on BK7258 is SYS_CPU_CURRENT_RUN_STATUS bit 9 (pwr_clk.c:331). NuttX
 * releases CPU1 asynchronously, from the rptun thread rptun_boot() creates,
 * and clears that bit before the release (bk7258_ap_control.c:419-423).
 * Waiting for it here makes "NuttX first" an ordering guarantee instead of
 * a consequence of thread priorities.
 *
 * rptun_wait() is not used: it needs the set_phase/get_phase ops, which
 * bk7258_rptun.c does not provide, and it spins.
 *
 * On a timeout Armino still starts, so a failed AP start does not take
 * Wi-Fi down with it; Armino may then boot CPU1 itself, the residual risk
 * S2.10 records.
 */

static void bk7258_armino_wait_ap(void)
{
  clock_t start = clock_systime_ticks();

  while ((getreg32(BK7258_SYS_CPU_STATUS) &
          BK7258_SYS_CPU1_PWR_DW_STATE) != 0)
    {
      if (clock_systime_ticks() - start >=
          MSEC2TICK(BK7258_ARMINO_AP_WAIT_MS))
        {
          syslog(LOG_WARNING, "[ARMINO] CPU1 still powered down after "
                 "%d ms; Armino may boot it itself\n",
                 BK7258_ARMINO_AP_WAIT_MS);
          return;
        }

      nxsig_usleep(BK7258_ARMINO_AP_POLL_US);
    }

  syslog(LOG_INFO, "[ARMINO] CPU1 powered up by NuttX after %lu ms\n",
         (unsigned long)TICK2MSEC(clock_systime_ticks() - start));
}

static int bk7258_armino_entry(int argc, char *argv[])
{
  struct mallinfo mm;

  (void)argc;
  (void)argv;

  /* Logged here, not at the end of bk7258_armino_early_initialize():
   * that runs on the idle task, and mallinfo() walks the heap, which
   * asserts !sched_idletask() (mm_foreach.c:94). Board boot 2026-10-03
   * reset on exactly that assert.
   */

  mm = mallinfo();
  syslog(LOG_INFO, "[ARMINO] heap arena=%lu free=%lu largest=%lu\n",
         (unsigned long)mm.arena, (unsigned long)mm.fordblks,
         (unsigned long)mm.mxordblk);

  bk7258_armino_wait_ap();
  entry_main();
  return 0;
}

int bk7258_armino_start(void)
{
  int ret;

  ret = kthread_create("armino-entry", BK7258_ARMINO_ENTRY_PRIORITY,
                       BK7258_ARMINO_ENTRY_STACKSIZE,
                       bk7258_armino_entry, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[ARMINO] entry thread failed: %d\n", ret);
      return ret;
    }

  return 0;
}

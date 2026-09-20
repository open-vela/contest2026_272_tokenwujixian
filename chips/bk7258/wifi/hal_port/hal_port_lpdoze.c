/* hal_port_lpdoze.c - PM/LPO/doze-family functions ported verbatim from
 * the authoritative Armino sources so the g_wifi_os_funcs answers are
 * byte-for-byte identical to the reference environment.
 *
 * Sources (function bodies copied, only the wrappers renamed):
 *   - cp/middleware/soc/bk7258/hal/sys_ll.h:      LL analog/ANA primitives
 *   - cp/middleware/soc/bk7258/soc/aon_pmu_ll.h:  R41 lpo_config/wakeup_ena
 *   - cp/middleware/soc/bk7258/hal/sys_hal.c:     DPLL SPI sequencer,
 *                                                 xtalh_ctune
 *   - cp/middleware/soc/bk7258/hal/sys_pm_hal.c:  mac wakeup source
 *   - cp/middleware/soc/bk7258/hal/aon_pmu_hal.c: wakeup source clear
 *   - cp/middleware/driver/sys_ctrl/sys_wifi_driver.c:
 *                                                 sys_drv_cali_dpll and
 *                                                 its delay helpers
 *   - cp/middleware/driver/pmu/aon_pmu_driver.c:  clear_wakeup_source
 */

#include <stdint.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <syslog.h>

#include <arch/chip/bk7258_memorymap.h>
#include <nuttx/spinlock.h>
#include <bk7258_irq.h>
#include "armino_compat.h"
#include "hal_port_sys_all.h"

/* ------------------------------------------------------------------ */
/* LL: analog register window                                          */
/* ------------------------------------------------------------------ */

/* This file used to re-implement the analog LL layer by hand: a local copy
 * of sys_ll_set_analog_reg_value()/sys_set_ana_reg_bit() plus one
 * hand-written accessor per ANA bitfield, each restating the register
 * index, bit position and mask as literals.
 *
 * All of that is deleted (2026-09-09).  The generated authority LL header
 * is imported verbatim and is byte-identical to the reference tree
 * (chips/bk7258/aon_pmu/armino/middleware/soc/bk7258/hal/sys_ll.h,
 * reached here through hal_port_sys_all.h), so every accessor these
 * function bodies need already exists with the authoritative bitfield
 * definition.  Re-typing those literals bought nothing and risked a silent
 * wrong-register write: a mistyped position or mask produces no error, no
 * log and no symptom.  hal_port_dvfs.c already called the authority
 * accessors directly and that path is hardware-verified (the post-init
 * readback reports vcore=0xb, matching the value written for the 60M
 * tier), so this file now follows the same rule -- call sys_ll_* and never
 * restate a bitfield.
 *
 * One behavioural consequence, stated explicitly because it is a real
 * tradeoff and not an oversight: the deleted local primitive capped the
 * analog-bus busy-wait at 1000 iterations and logged on expiry, whereas
 * the authority primitive spins until the SPI busy bit clears.  Aligning
 * means adopting the unbounded spin.  A stuck analog SPI bus is a hardware
 * fault in which continuing with a half-written analog register is worse
 * than stopping, and the same primitive is already live on the DVFS path
 * without ever hanging.
 */

/* ------------------------------------------------------------------ */
/* LL: AON PMU R41 lpo_config / wakeup_ena (aon_pmu_ll.h)              */
/* ------------------------------------------------------------------ */

uint32_t hp_aon_pmu_ll_get_r41_lpo_config(void)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  return r->lpo_config;
}

void hp_aon_pmu_ll_set_r41_lpo_config(uint32_t v)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  r->lpo_config = v;
}

uint32_t hp_aon_pmu_ll_get_r41_wakeup_ena(void)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  return r->wakeup_ena;
}

void hp_aon_pmu_ll_set_r41_wakeup_ena(uint32_t v)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  r->wakeup_ena = v;
}

/* ------------------------------------------------------------------ */
/* DRV: delay helpers (sys_wifi_driver.c, constants as upstream)       */
/* ------------------------------------------------------------------ */

#define HP_SYS_DRV_DELAY_TIME_10US   120u   /* SYS_DRV_DELAY_TIME_10US */
#define HP_SYS_DRV_DELAY_TIME_200US  3400u  /* SYS_DRV_DELAY_TIME_200US */

void hp_sys_drv_delay10us(void)
{
  volatile uint32_t i;

  for (i = 0; i < HP_SYS_DRV_DELAY_TIME_10US; i++)
    ;
}

void hp_sys_drv_delay200us(void)
{
  volatile uint32_t i;

  for (i = 0; i < HP_SYS_DRV_DELAY_TIME_200US; i++)
    ;
}

void hp_sys_drv_ps_dpll_delay(uint32_t time)
{
  volatile uint32_t i;

  for (i = 0; i < time; i++)
    ;
}

/* ------------------------------------------------------------------ */
/* HAL: DPLL calibration SPI sequencer (sys_hal.c:1751-1769)           */
/* ------------------------------------------------------------------ */

void hp_sys_hal_cali_dpll_spi_trig_disable(void)
{
  sys_ll_set_ana_reg0_spitrig(0);
}

void hp_sys_hal_cali_dpll_spi_trig_enable(void)
{
  sys_ll_set_ana_reg0_spitrig(1);
}

void hp_sys_hal_cali_dpll_spi_detect_disable(void)
{
  sys_ll_set_ana_reg0_spideten(0);
}

void hp_sys_hal_cali_dpll_spi_detect_enable(void)
{
  sys_ll_set_ana_reg0_spideten(1);
}

/* ------------------------------------------------------------------ */
/* DRV: sys_drv_cali_dpll (sys_wifi_driver.c:49, full upstream body)   */
/* ------------------------------------------------------------------ */

uint32_t hp_sys_drv_cali_dpll(uint32_t param)
{
  static spinlock_t lock = SP_UNLOCKED;
  irqstate_t int_level = spin_lock_irqsave(&lock);

  hp_sys_hal_cali_dpll_spi_trig_disable();

  if (!param)
    {
      hp_sys_drv_delay10us();
    }
  else
    {
      hp_sys_drv_ps_dpll_delay(60);
    }

  hp_sys_hal_cali_dpll_spi_trig_enable();
  hp_sys_hal_cali_dpll_spi_detect_disable();

  if (!param)
    {
      hp_sys_drv_delay200us();
    }
  else
    {
      hp_sys_drv_ps_dpll_delay(340);
    }

  hp_sys_hal_cali_dpll_spi_detect_enable();

  spin_unlock_irqrestore(&lock, int_level);
  return 0;
}

/* ------------------------------------------------------------------ */
/* HAL/PM: MAC wakeup source (sys_pm_hal.c:1249 / aon_pmu_hal.c:49)    */
/* wakeup_source_t: GPIO=0 RTC=1 WIFI=2 BT=3 USBPLUG=4 TOUCHED=5       */
/* ------------------------------------------------------------------ */

void hp_sys_hal_enable_mac_wakeup_source(void)
{
  uint32_t wakeup_ena = hp_aon_pmu_ll_get_r41_wakeup_ena();

  wakeup_ena |= (UINT32_C(1) << 2); /* WAKEUP_SOURCE_INT_WIFI */
  hp_aon_pmu_ll_set_r41_wakeup_ena(wakeup_ena);
}

void hp_aon_pmu_hal_clear_wakeup_source(uint32_t value)
{
  uint32_t wakeup_source = hp_aon_pmu_ll_get_r41_wakeup_ena();

  wakeup_source &= ~(UINT32_C(0x1) << value);
  hp_aon_pmu_ll_set_r41_wakeup_ena(wakeup_source);
}

/* hp_sys_hal_enter_low_analog()/hp_sys_hal_exit_low_analog() removed
 * 2026-09-09: sys_pm_hal.c is now imported whole under pm/authority, so the
 * upstream definitions (sys_pm_hal.c:1281,1294) are compiled directly and
 * this local transcription is redundant.  exit_low_analog branches on
 * ana_reg11.aldosel, which the same file establishes through
 * sys_hal_low_power_hardware_init() -> sys_hal_enable_buck(); porting the leaf
 * without that initialisation was the defect this import closes.
 */

/* ------------------------------------------------------------------ */
/* HAL/DRV: bandgap calibration trim                                   */
/*   cp/middleware/soc/bk7258/hal/sys_hal.c:1938  sys_hal_get_bgcalm   */
/*   cp/middleware/soc/bk7258/hal/sys_hal.c:1943  sys_hal_set_bgcalm   */
/*   cp/middleware/driver/sys_ctrl/sys_wifi_driver.c:344 sys_drv_get_  */
/*   cp/middleware/driver/sys_ctrl/sys_wifi_driver.c:354 sys_drv_set_  */
/*                                                                     */
/* The PHY function table binds ._sys_drv_set_bgcalm/._sys_drv_get_    */
/* bgcalm unconditionally (bk_phy_adapter.c:651-652 -- no              */
/* CONFIG_SOC_BK7256XX guard, unlike the eFuse entry), so these are    */
/* live calls from libbk_phy.a, not dead table slots.  They used to    */
/* resolve to log-only stubs in hal_port/analog_shim.c that returned 0 /   */
/* BK_FAIL and touched no hardware; the PHY's bandgap trim therefore   */
/* silently did nothing.  bandgap_init() (components_init.c:97) also   */
/* has sys_drv_set_bgcalm() as its whole point, so importing it before */
/* this existed would have been a no-op dressed up as alignment.       */
/* ------------------------------------------------------------------ */

static uint32_t hp_sys_hal_get_bgcalm(void)
{
  return sys_ll_get_ana_reg8_bgcal();
}

static void hp_sys_hal_set_bgcalm(uint32_t value)
{
  sys_ll_set_ana_reg9_spi_latch1v(1);
  sys_ll_set_ana_reg8_bgcal(value);
  sys_ll_set_ana_reg9_spi_latch1v(0);
}

uint32_t hp_sys_drv_get_bgcalm(void)
{
  static spinlock_t lock = SP_UNLOCKED;
  irqstate_t int_level = spin_lock_irqsave(&lock);
  uint32_t ret;

  ret = hp_sys_hal_get_bgcalm();

  spin_unlock_irqrestore(&lock, int_level);
  return ret;
}

uint32_t hp_sys_drv_set_bgcalm(uint32_t param)
{
  static spinlock_t lock = SP_UNLOCKED;
  irqstate_t int_level = spin_lock_irqsave(&lock);

  hp_sys_hal_set_bgcalm(param);

  spin_unlock_irqrestore(&lock, int_level);

  /* Upstream returns SYS_DRV_SUCCESS (sys_driver.h:28), which is 0.  Spelled
   * as a literal rather than BK_OK because this translation unit does not
   * pull in common/bk_err.h -- the same reason hp_sys_drv_cali_dpll() above
   * returns 0. */

  return 0;
}

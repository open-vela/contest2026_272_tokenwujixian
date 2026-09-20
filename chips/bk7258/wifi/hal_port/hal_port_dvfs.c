/* hal_port_dvfs.c - CPU/bus DVFS ported from Armino verbatim.
 *
 * Source chain (Armino pristine v3.1.1, cp/):
 *   components/bk_pm/pm.c:2218          bk_pm_module_vote_cpu_freq
 *   middleware/driver/sys_ctrl/
 *     sys_ps_driver.c:244               sys_drv_switch_cpu_bus_freq
 *   middleware/soc/bk7258/hal/
 *     sys_hal.c:711                     sys_hal_switch_cpu_bus_freq
 *     sys_hal.c:643                     ..._low_to_high
 *     sys_hal.c:571                     ..._high_to_low
 *     sys_hal.c:430                     sys_hal_core_bus_clock_ctrl
 *     sys_hal.c:553                     sys_hal_ctrl_vdddig_h_vol
 *     sys_hal.c:517                     sys_hal_ctrl_vddd_h_vol
 *
 * Why this exists: our bk_pm_module_vote_cpu_freq was a syslog stub that
 * never touched the clock tree or the core voltage.  The authoritative
 * boot path votes 120M in bk_init (components/bk_init/bk_init.c:267),
 * which walks the frequency ladder and -- the part that actually matters
 * for the analog domains -- programs vdddig to 0.9V along the way.
 *
 * The functions below are structural copies: same register fields, same
 * ordering, same ladder, same static initializers (including the
 * deliberate disagreement between the driver-level 26M and the HAL-level
 * 120M seeds, which is what produces the authoritative 120->60->80->120
 * trajectory on the first vote).
 *
 * Build-config resolution against the authoritative bk7258 iperf build
 * (build/bk7258/iperf/bk7258/config/sdkconfig.h -- all three unset):
 *   CONFIG_DCO_CLK_ENABLE  unset -> 120M uses core_bus_clock_ctrl(3,3,0,1,1)
 *   CONFIG_ATE_TEST        unset -> 120M uses vdddig 0xC (0.9V)
 *   CONFIG_RX_OPTIMIZE     unset -> sys_hal_ctrl_vddd_h_vol body is empty
 * The dead branches are dropped rather than carried as #if 0.
 */

#include <stdint.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>
/* enter/leave_critical_section are macros here (spinlock.h:1541 on a
 * non-SMP build); without this header they degrade to implicit calls to
 * symbols that do not exist and only fail at link time. */
#include <nuttx/spinlock.h>
#include <nuttx/arch.h>
#include <syslog.h>

/* bk_include.h first: modules/pm.h uses the vendor int32/uint32 aliases
 * that bk_typedef.h defines (same order as hal_port/platform_shim.c). */
#include <common/bk_include.h>
#include <common/bk_err.h>
#include <modules/pm.h>

#include "hal_port_sys_all.h"

/* ------------------------------------------------------------------ */
/* Constants (sys_hal.c:34-43 file-local defines, sys_types.h:141-143) */
/* ------------------------------------------------------------------ */

#define HP_PM_CLKSEL_CORE_320M              (2)
#define HP_PM_CLKSEL_CORE_480M              (3)
#define HP_PM_CLKDIV_CORE_0                 (0)
#define HP_PM_CLKDIV_CORE_1                 (1)
#define HP_PM_VDDD_H_VOL_1V                 (0x6)
#define HP_PM_VDDDIG_H_VOL_0v9              (0xC)
#define HP_PM_CLKDV_CPU1_1                  (0x1)
#define HP_PM_CLKDV_CPU0_0                  (0x0)

#define HP_PM_FREQUNCY_DIV_MAX              (15)
#define HP_PM_FREQUNCY_DIV_BUS_MAX          (1)
#define HP_PM_FREQUNCY_DIV_CPU_MAX          (1)

/* sys_hal.c uses a calibrated busy loop of 2600 iterations here and
 * documents the intent as "delay 10uS for voltage stability".  A busy
 * loop is not portable across our compiler/frequency, so this uses the
 * NuttX calibrated delay with margin -- overshooting a voltage settling
 * window is safe, undershooting is not. */
#define HP_SWITCH_VDDDIG_VOL_DELAY_US       (20)

/* ------------------------------------------------------------------ */
/* clk div register access (sys_hal.c:334-352)                         */
/* ------------------------------------------------------------------ */

/* CLK_DIV_ADDRESS_MAP (sys_types.h:520) resolves CLK_DIV_REG0 to
 * SYS_CPU_CLK_DIV_MODE1_ADDR.  Only REG0 is on the DVFS path, so the
 * table indirection collapses to the one address.
 *
 * Defined locally rather than pulled from sys_reg.h: that header is a
 * 114 KB address-macro dump shared with the working lpdoze port, and one
 * macro is not worth widening its exposure.  Value matches sys_reg.h:178
 * and resolves to 0x44010020 -- the register the scan diag already reads. */
#define HP_CLK_DIV_REG0_ADDR   (SOC_SYS_REG_BASE + (0x8u << 2))

static inline uint32_t hp_clk_div_reg0_get(void)
{
  return REG_READ(HP_CLK_DIV_REG0_ADDR);
}

static inline void hp_clk_div_reg0_set(uint32_t value)
{
  REG_WRITE(HP_CLK_DIV_REG0_ADDR, value);
}

/* sys_hal_cpu_clk_div_set (sys_hal.c:354) */
static void hp_sys_hal_cpu_clk_div_set(uint32_t core_index, uint32_t value)
{
  if (core_index == 0)
    {
      sys_ll_set_cpu0_int_halt_clk_op_cpu0_speed(value);
    }
  else if (core_index == 1)
    {
      sys_ll_set_cpu1_int_halt_clk_op_cpu1_speed(value);
    }
  else if (core_index == 2)
    {
      sys_ll_set_cpu2_int_halt_clk_op_cpu2_speed(value);
    }
}

/* ------------------------------------------------------------------ */
/* core voltage (sys_hal.c:517-568)                                    */
/* ------------------------------------------------------------------ */

/* sys_hal_ctrl_vddd_h_vol (sys_hal.c:517): the whole body is behind
 * CONFIG_RX_OPTIMIZE, which is unset in the authoritative build.  Kept
 * as a no-op so the call sites stay verbatim. */
static void hp_sys_hal_ctrl_vddd_h_vol(uint32_t vol_value)
{
  (void)vol_value;
}

/* sys_hal_ctrl_vdddig_h_vol (sys_hal.c:553) */
static void hp_sys_hal_ctrl_vdddig_h_vol(uint32_t vol_value)
{
  if (sys_ll_get_ana_reg9_vcorehsel() != vol_value)
    {
      sys_ll_set_ana_reg9_spi_latch1v(1);
      sys_ll_set_ana_reg9_vcorehsel(vol_value);
      sys_ll_set_ana_reg9_spi_latch1v(0);
      up_udelay(HP_SWITCH_VDDDIG_VOL_DELAY_US);
    }
}

/* sys_hal_vdddig_h_vol_get (sys_hal.c:568) */
static uint32_t hp_sys_hal_vdddig_h_vol_get(void)
{
  return sys_ll_get_ana_reg9_vcorehsel();
}

/* ------------------------------------------------------------------ */
/* VDD core-voltage accessors on the PHY function table                */
/*   cp/middleware/soc/bk7258/hal/sys_hal.c:1687  sys_hal_set_vdd_value */
/*   cp/middleware/soc/bk7258/hal/sys_hal.c:1692  sys_hal_get_vdd_value */
/*   cp/middleware/driver/sys_ctrl/sys_wifi_driver.c:549 sys_drv_set_   */
/*   cp/middleware/driver/sys_ctrl/sys_wifi_driver.c:559 sys_drv_get_   */
/*                                                                     */
/* bk_phy_adapter.c:653-654 binds ._sys_drv_set_vdd_value /            */
/* ._sys_drv_get_vdd_value with no CONFIG_SOC_BK7256XX guard, so these  */
/* are live calls from libbk_phy.a.  They previously resolved to        */
/* log-only stubs in hal_port/analog_shim.c (return 0 / BK_FAIL, no         */
/* hardware access).  Upstream set_vdd_value is exactly a call to       */
/* sys_hal_ctrl_vdddig_h_vol(), which this file already ports verbatim  */
/* and which is hardware-verified: the post-init readback reports       */
/* vcore=0xb, the value written for the 60M tier.                       */
/* ------------------------------------------------------------------ */

uint32_t hp_sys_drv_get_vdd_value(void)
{
  irqstate_t flags = enter_critical_section();
  uint32_t ret;

  ret = hp_sys_hal_vdddig_h_vol_get();

  leave_critical_section(flags);
  return ret;
}

uint32_t hp_sys_drv_set_vdd_value(uint32_t param)
{
  irqstate_t flags = enter_critical_section();

  hp_sys_hal_ctrl_vdddig_h_vol(param);

  leave_critical_section(flags);

  /* Upstream returns SYS_DRV_SUCCESS (sys_driver.h:28) == 0 == BK_OK. */

  return BK_OK;
}

/* ------------------------------------------------------------------ */
/* sys_hal_core_bus_clock_ctrl (sys_hal.c:430)                         */
/* ------------------------------------------------------------------ */

static bk_err_t hp_sys_hal_core_bus_clock_ctrl(uint32_t cksel_core,
                                               uint32_t ckdiv_core,
                                               uint32_t ckdiv_bus,
                                               uint32_t ckdiv_cpu0,
                                               uint32_t ckdiv_cpu1)
{
  uint32_t clk_param;
  uint32_t h_vol;

  if (cksel_core > 3)
    {
      return BK_FAIL;
    }

  if (ckdiv_core > HP_PM_FREQUNCY_DIV_MAX ||
      ckdiv_bus > HP_PM_FREQUNCY_DIV_BUS_MAX ||
      ckdiv_cpu1 > HP_PM_FREQUNCY_DIV_CPU_MAX ||
      ckdiv_cpu0 > HP_PM_FREQUNCY_DIV_CPU_MAX)
    {
      return BK_FAIL;
    }

  if (cksel_core == HP_PM_CLKSEL_CORE_320M &&
      ckdiv_core == HP_PM_CLKDIV_CORE_0 &&
      ckdiv_cpu0 != HP_PM_CLKDV_CPU0_0)
    {
      return BK_FAIL;
    }

  if (cksel_core == HP_PM_CLKSEL_CORE_320M &&
      ckdiv_core == HP_PM_CLKDIV_CORE_0)
    {
      h_vol = hp_sys_hal_vdddig_h_vol_get();
      if (h_vol < HP_PM_VDDDIG_H_VOL_0v9)
        {
          hp_sys_hal_ctrl_vddd_h_vol(HP_PM_VDDD_H_VOL_1V);
          hp_sys_hal_ctrl_vdddig_h_vol(HP_PM_VDDDIG_H_VOL_0v9);
        }
    }

  clk_param = hp_clk_div_reg0_get();
  if (((clk_param >> 0x4) & 0x3) > cksel_core)
    {
      /* higher frequency -> lower frequency */

      /* 1. core clk select */
      clk_param = hp_clk_div_reg0_get();
      clk_param &= ~(0x3 << 4);
      clk_param |= cksel_core << 4;
      hp_clk_div_reg0_set(clk_param);

      /* 2. config bus and core clk div */
      clk_param = hp_clk_div_reg0_get();
      clk_param &= ~(0xf << 0);
      clk_param |= ckdiv_core << 0;
      hp_clk_div_reg0_set(clk_param);

      /* 3. config cpu clk div */
      hp_sys_hal_cpu_clk_div_set(0, ckdiv_cpu0);
      hp_sys_hal_cpu_clk_div_set(1, ckdiv_cpu1);
      hp_sys_hal_cpu_clk_div_set(2, 1);
    }
  else
    {
      /* lower frequency -> higher frequency */

      /* 1. config bus and core clk div */
      if (ckdiv_core == 0)
        {
          /* avoid the bus freq > 240m */

          hp_sys_hal_cpu_clk_div_set(0, ckdiv_cpu0);
        }

      clk_param = hp_clk_div_reg0_get();
      clk_param &= ~(0xf << 0);
      clk_param |= ckdiv_core << 0;
      hp_clk_div_reg0_set(clk_param);

      /* 2. config cpu clk div */
      if (ckdiv_core != 0)
        {
          hp_sys_hal_cpu_clk_div_set(0, ckdiv_cpu0);
        }

      hp_sys_hal_cpu_clk_div_set(1, ckdiv_cpu1);
      hp_sys_hal_cpu_clk_div_set(2, 1);

      /* 3. core clk select */
      clk_param = hp_clk_div_reg0_get();
      clk_param &= ~(0x3 << 4);
      clk_param |= cksel_core << 4;
      hp_clk_div_reg0_set(clk_param);
    }

  return BK_OK;
}

/* ------------------------------------------------------------------ */
/* frequency ladder (sys_hal.c:571 / :643)                             */
/* ------------------------------------------------------------------ */

/* sys_hal_switch_cpu_bus_freq_high_to_low (sys_hal.c:571) */
static bk_err_t hp_switch_freq_high_to_low(pm_cpu_freq_e cpu_bus_freq)
{
  bk_err_t ret = BK_OK;

  switch (cpu_bus_freq)
    {
      case PM_CPU_FRQ_480M:  /* cpu0:240m cpu1:480m cpu2:480m bus:240m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x0, 0x0, 0x0, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x7);
        hp_sys_hal_ctrl_vdddig_h_vol(0xe);
        break;

      case PM_CPU_FRQ_320M:  /* cpu0:160m cpu1:320m cpu2:320m bus:160m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x2, 0x0, 0x0, 0x0, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x7);
        hp_sys_hal_ctrl_vdddig_h_vol(0xe);
        break;

      case PM_CPU_FRQ_240M:  /* cpu0:240m cpu1:240m cpu2:240m bus:240m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x1, 0x0, 0x1, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xd);
        break;

      case PM_CPU_FRQ_120M:  /* cpu0:120m cpu1:120m cpu2:120m bus:120m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x3, 0x0, 0x1, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xc);
        break;

      case PM_CPU_FRQ_80M:   /* cpu0:80m cpu1:80m cpu2:80m bus:80m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x5, 0x0, 0x1, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xb);
        break;

      case PM_CPU_FRQ_60M:   /* cpu0:60m cpu1:60m cpu2:60m bus:60m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x7, 0x0, 0x1, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xb);
        break;

      case PM_CPU_FRQ_26M:   /* cpu0:26m cpu1:26m cpu2:26m bus:26m */
        ret = hp_sys_hal_core_bus_clock_ctrl(0x0, 0x0, 0x0, 0x1, 0x1);
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xb);
        break;

      default:
        break;
    }

  return ret;
}

/* sys_hal_switch_cpu_bus_freq_low_to_high (sys_hal.c:643) -- same pairs,
 * voltage raised before the clock instead of after. */
static bk_err_t hp_switch_freq_low_to_high(pm_cpu_freq_e cpu_bus_freq)
{
  bk_err_t ret = BK_OK;

  switch (cpu_bus_freq)
    {
      case PM_CPU_FRQ_480M:
        hp_sys_hal_ctrl_vddd_h_vol(0x7);
        hp_sys_hal_ctrl_vdddig_h_vol(0xe);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x0, 0x0, 0x0, 0x1);
        break;

      case PM_CPU_FRQ_320M:
        hp_sys_hal_ctrl_vddd_h_vol(0x7);
        hp_sys_hal_ctrl_vdddig_h_vol(0xe);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x2, 0x0, 0x0, 0x0, 0x1);
        break;

      case PM_CPU_FRQ_240M:
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xd);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x1, 0x0, 0x1, 0x1);
        break;

      case PM_CPU_FRQ_120M:
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xc);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x3, 0x0, 0x1, 0x1);
        break;

      case PM_CPU_FRQ_80M:
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xb);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x5, 0x0, 0x1, 0x1);
        break;

      case PM_CPU_FRQ_60M:
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xb);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x3, 0x7, 0x0, 0x1, 0x1);
        break;

      case PM_CPU_FRQ_26M:
        hp_sys_hal_ctrl_vddd_h_vol(0x6);
        hp_sys_hal_ctrl_vdddig_h_vol(0xb);
        ret = hp_sys_hal_core_bus_clock_ctrl(0x0, 0x0, 0x0, 0x1, 0x1);
        break;

      default:
        break;
    }

  return ret;
}

/* sys_hal_switch_cpu_bus_freq (sys_hal.c:710).  The 120M seed is
 * verbatim and intentional: it disagrees with the driver-level 26M seed
 * below, and that disagreement is what makes the first 120M vote walk
 * down to 60M and back up -- the authoritative register trajectory. */
static pm_cpu_freq_e s_hp_pre_cpu_freq = PM_CPU_FRQ_120M;

static bk_err_t hp_sys_hal_switch_cpu_bus_freq(pm_cpu_freq_e cpu_bus_freq)
{
  pm_cpu_freq_e prev_freq = s_hp_pre_cpu_freq;

  if (prev_freq == cpu_bus_freq)
    {
      return BK_OK;
    }

  if (prev_freq > cpu_bus_freq)
    {
      hp_switch_freq_high_to_low(cpu_bus_freq);
    }
  else
    {
      hp_switch_freq_low_to_high(cpu_bus_freq);
    }

  s_hp_pre_cpu_freq = cpu_bus_freq;
  return BK_OK;
}

/* ------------------------------------------------------------------ */
/* sys_drv_switch_cpu_bus_freq (sys_ps_driver.c:243)                   */
/* ------------------------------------------------------------------ */

static pm_cpu_freq_e s_hp_cpu_freq = PM_CPU_FRQ_26M;

bk_err_t hp_sys_drv_switch_cpu_bus_freq(pm_cpu_freq_e cpu_bus_freq)
{
  bk_err_t ret = BK_FAIL;
  irqstate_t flags;
  int32_t i;

  if (s_hp_cpu_freq == cpu_bus_freq)
    {
      return BK_OK;
    }

  switch (cpu_bus_freq)
    {
      case PM_CPU_FRQ_480M:
      case PM_CPU_FRQ_320M:
      case PM_CPU_FRQ_240M:
      case PM_CPU_FRQ_120M:
      case PM_CPU_FRQ_80M:
      case PM_CPU_FRQ_60M:
      case PM_CPU_FRQ_26M:
        break;

      default:
        return BK_FAIL;
    }

  flags = enter_critical_section();

  /* Walk one step at a time so every intermediate voltage/clock pair is
   * applied in order (sys_ps_driver.c:269-283).
   *
   * Deviation from verbatim: Armino declares this counter uint32_t and
   * tests `i >= cpu_bus_freq`.  With a 26M target (PM_CPU_FRQ_26M == 0)
   * the final `i--` wraps to 0xFFFFFFFF, the condition stays true, and
   * the loop spins forever feeding out-of-range tiers to the HAL.  Latent
   * upstream (nothing on our path votes 26M), but a signed counter gives
   * the same trajectory without the hang. */
  if ((int32_t)s_hp_cpu_freq < (int32_t)cpu_bus_freq)
    {
      for (i = (int32_t)s_hp_cpu_freq + 1; i <= (int32_t)cpu_bus_freq; i++)
        {
          ret = hp_sys_hal_switch_cpu_bus_freq((pm_cpu_freq_e)i);
        }
    }
  else
    {
      for (i = (int32_t)s_hp_cpu_freq - 1; i >= (int32_t)cpu_bus_freq; i--)
        {
          ret = hp_sys_hal_switch_cpu_bus_freq((pm_cpu_freq_e)i);
        }
    }

  s_hp_cpu_freq = cpu_bus_freq;
  leave_critical_section(flags);

  return ret;
}

/* ------------------------------------------------------------------ */
/* vote bookkeeping (pm.c:2218)                                        */
/* ------------------------------------------------------------------ */

static uint8_t s_hp_pm_cpu_freq[PM_DEV_ID_MAX];
static pm_cpu_freq_e s_hp_current_cpu_freq = PM_CPU_FRQ_26M;

bk_err_t hp_pm_module_vote_cpu_freq(pm_dev_id_e module,
                                    pm_cpu_freq_e cpu_freq)
{
  bk_err_t ret = BK_OK;
  irqstate_t flags;
  uint32_t freq_max;
  uint32_t i;

  if ((uint32_t)module >= PM_DEV_ID_MAX)
    {
      return BK_FAIL;
    }

  flags = enter_critical_section();

  if (cpu_freq == PM_CPU_FRQ_DEFAULT)
    {
      /* falls back to whatever PM_DEV_ID_DEFAULT voted (pm.c:2238) */

      cpu_freq = PM_CPU_FRQ_26M;
    }

  s_hp_pm_cpu_freq[module] = (uint8_t)cpu_freq;

  freq_max = s_hp_pm_cpu_freq[0];
  for (i = 1; i < PM_DEV_ID_MAX; i++)
    {
      if (freq_max < s_hp_pm_cpu_freq[i])
        {
          freq_max = s_hp_pm_cpu_freq[i];
        }
    }

  if (s_hp_current_cpu_freq == (pm_cpu_freq_e)freq_max)
    {
      leave_critical_section(flags);
      return BK_OK;
    }

  /* The authority permits 320M/480M votes. Its PM bookkeeping records the
   * CPU-frequency VDDDIG vote before calling sys_drv_switch_cpu_bus_freq();
   * the BK7258 ladder below then raises VDDDIG before each high-frequency
   * clock step. This port already carries that ladder, so rejecting the vote
   * here caused the closed Wi-Fi path's live freq=5 request to fail without
   * ever reaching the authoritative voltage/clock transition. */
  if (freq_max == PM_CPU_FRQ_480M || freq_max == PM_CPU_FRQ_320M)
    {
      syslog(LOG_INFO,
             "[BK7258-WIFI] dvfs: high-tier VDDDIG/clock vote freq=%u\n",
             (unsigned)freq_max);
    }

  ret = hp_sys_drv_switch_cpu_bus_freq((pm_cpu_freq_e)freq_max);
  if (ret == BK_OK)
    {
      s_hp_current_cpu_freq = (pm_cpu_freq_e)freq_max;
    }

  leave_critical_section(flags);

  return ret;
}

/* Boot-time observation point: reports the live CLK_DIV_REG0 and the
 * core voltage select so the board log states the actual clock/voltage
 * rather than an assumed one. */
void hp_dvfs_log_state(const char *tag)
{
  uint32_t reg0 = hp_clk_div_reg0_get();

  /* Two short lines, not one wide one.  The operator console is 80 columns
   * and overwrites past ~79 chars (the previous single line lost exactly the
   * vcorehsel field -- the only value here that was still unknown -- on two
   * consecutive boards).  Budget: keep payload under ~60 chars after the
   * 14-char "[BK7258-WIFI] " tag. */
  syslog(LOG_INFO, "[BK7258-WIFI] dvfs %s r0=0x%08lx vcore=0x%lx\n",
         tag, (unsigned long)reg0,
         (unsigned long)hp_sys_hal_vdddig_h_vol_get());
  syslog(LOG_INFO, "[BK7258-WIFI] dvfs %s sel=%lu div=%lu bus=%lu\n",
         tag,
         (unsigned long)((reg0 >> 4) & 0x3),
         (unsigned long)(reg0 & 0xf),
         (unsigned long)((reg0 >> 6) & 0x1));
}

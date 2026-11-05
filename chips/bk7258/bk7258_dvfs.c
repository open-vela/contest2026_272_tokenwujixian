/****************************************************************************
 * chips/bk7258/bk7258_dvfs.c
 *
 * Shared CPU/bus operating points, in NuttX shape.
 *
 * BK7258 has one shared clock tree: CPU0 (the CP) and CPU1/CPU2 (the AP) are
 * programmed together, and the AP never sets its own frequency.  Reference
 * implementations in the field all agree on this and on the register fields:
 * the peer repository contest2026_135_yongwangzhiqian carries a NuttX rewrite
 * (chips/bk7258/cp/bk7258_dvfs.c, Apache-2.0, "mirrors the Armino SDK runtime
 * clock path") and contest2026_264_VelaSight drives the Armino source directly
 * from pwr_clk.c.  This file is that layer for us: seven steps, the vendor's
 * field order, and the vendor's LL setters -- no hand-computed divider values.
 *
 *   OPP      CPU0     CPU1/2   bus      cksel_core  clkdiv_core  cpu0_speed
 *   26M       26        26      26           0            0          1
 *   60M       60        60      60           3            7          1
 *   80M       80        80      80           3            5          1
 *   120M     120       120     120           3            3          1
 *   240M     240       240     240           3            1          1
 *   320M     160       320     160           2            0          0
 *   480M     240       480     240           3            0          0
 *
 * Two things are easy to get wrong and both are encoded below:
 *
 *   - cpu0_speed 1 means "/1" and 0 means "/2".  Reading it the other way
 *     round halves the AP: measured CPU0 236 MHz against CPU1 119 MHz.
 *   - CPU1/CPU2 are ALWAYS set to /1 while CPU0 comes from the table, which is
 *     what makes the 320M and 480M points run the AP faster than the CP.
 *     ckdiv_cpu1 is a one-bit field: there is no finer CPU1 divider to reach
 *     for, and the AP is never divided below /1.
 *
 * The AP image must not touch any of this: the same word that carries CPU1's
 * speed also carries its entry offset (bits[8:31]), so a write from the AP
 * costs the core its clock and its restart vector (measured: 60 MHz, no
 * recovery).
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_COMPONENT_CP

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "include/bk7258_dvfs.h"

#include <sys_ll.h>
#include <sys_hal.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CONFIG_RX_OPTIMIZE gates the vendor's VDDD (vdighsel) raise; VDDIG
 * (vcorehsel) is unconditional.  Declared here because the authority keeps
 * these two prototypes inside sys_hal.c rather than a public header.
 */

bk_err_t sys_hal_ctrl_vddd_h_vol(uint32_t vol_value);
bk_err_t sys_hal_ctrl_vdddig_h_vol(uint32_t vol_value);

struct bk7258_dvfs_step_s
{
  uint8_t cksel_core;                     /* CLK_DIV_MODE1 cksel_core[5:4]   */
  uint8_t clkdiv_core;                    /* CLK_DIV_MODE1 clkdiv_core[3:0]  */
  uint8_t cpu0_speed;                     /* CPU0 speed bit: 1 = /1, 0 = /2  */
  uint8_t vddd;                           /* ANA_REG9 vdighsel raw           */
  uint8_t vddig;                          /* ANA_REG9 vcorehsel raw          */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bk7258_dvfs_step_s g_bk7258_dvfs_steps[] =
{
  /* cksel clkdiv core0  vddd  vddig */
  { 0x0,   0x0,   0x1,   0x6,  0xB },   /* OPP 0:  26/ 26/ 26 MHz */
  { 0x3,   0x7,   0x1,   0x6,  0xB },   /* OPP 1:  60/ 60/ 60 MHz */
  { 0x3,   0x5,   0x1,   0x6,  0xB },   /* OPP 2:  80/ 80/ 80 MHz */
  { 0x3,   0x3,   0x1,   0x6,  0xC },   /* OPP 3: 120/120/120 MHz */
  { 0x3,   0x1,   0x1,   0x6,  0xD },   /* OPP 4: 240/240/240 MHz */
  { 0x2,   0x0,   0x0,   0x7,  0xE },   /* OPP 5: 160/320/160 MHz */
  { 0x3,   0x0,   0x0,   0x7,  0xE },   /* OPP 6: 240/480/240 MHz */
};

/* The table is indexed by the operating point, so a missing or extra row would
 * silently label the wrong frequency.
 */

_Static_assert(sizeof(g_bk7258_dvfs_steps) / sizeof(g_bk7258_dvfs_steps[0]) ==
               BK7258_OPP_MAX + 1,
               "DVFS table must have exactly one row per operating point");

static const uint32_t g_bk7258_cpu0_hz[] = BK7258_OPP_CPU0_HZ;
static const uint32_t g_bk7258_ap_hz[]   = BK7258_OPP_AP_HZ;
static const uint32_t g_bk7258_bus_hz[]  = BK7258_OPP_BUS_HZ;

_Static_assert(sizeof(g_bk7258_cpu0_hz) / sizeof(g_bk7258_cpu0_hz[0]) ==
               BK7258_OPP_MAX + 1,
               "CPU0 table must have one row per operating point");
_Static_assert(sizeof(g_bk7258_ap_hz) / sizeof(g_bk7258_ap_hz[0]) ==
               BK7258_OPP_MAX + 1,
               "AP table must have one row per operating point");

/* And the documented frequencies have to agree with the rows: the 320M and
 * 480M points are the ones where the AP outruns the CP, which is the whole
 * reason for the separate cpu0_speed field.
 */

_Static_assert(BK7258_OPP_CPU0_320M_HZ < BK7258_OPP_CPU0_240M_HZ,
               "320M must slow CPU0 down to 160 MHz");
_Static_assert(BK7258_OPP_AP_480M_HZ == 2u * BK7258_OPP_CPU0_480M_HZ,
               "480M must run the AP at twice CPU0");

static int g_bk7258_dvfs_opp = BK7258_OPP_26M;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_dvfs_write_cksel_core(uint8_t v)
{
  sys_ll_set_cpu_clk_div_mode1_cksel_core(v);
}

static void bk7258_dvfs_write_clkdiv_core(uint8_t v)
{
  sys_ll_set_cpu_clk_div_mode1_clkdiv_core(v);
}

static void bk7258_dvfs_write_cpu0_speed(uint8_t v)
{
  sys_ll_set_cpu0_int_halt_clk_op_cpu0_speed(v);
}

/* CPU1/CPU2 take their divider from the same one-bit field; the authority's
 * step routine sets both to /1 unconditionally.
 */

static void bk7258_dvfs_undivide_ap(void)
{
  sys_ll_set_cpu1_int_halt_clk_op_cpu1_speed(1);
#if defined(SOC_CPU_CNT) && SOC_CPU_CNT > 2
  sys_ll_set_cpu2_int_halt_clk_op_cpu2_speed(1);
#endif
}

static void bk7258_dvfs_barrier(void)
{
  __asm__ volatile ("dsb" : : : "memory");
  __asm__ volatile ("isb" : : : "memory");
}

/* Going up: raise the rails first, then the dividers, then the mux.  The
 * cpu0_speed write moves ahead of clkdiv_core when clkdiv_core is 0 so the bus
 * never exceeds 240 MHz in between (authority sys_hal.c low_to_high order).
 */

static void bk7258_dvfs_step_up(const struct bk7258_dvfs_step_s *s)
{
  sys_hal_ctrl_vddd_h_vol(s->vddd);
  sys_hal_ctrl_vdddig_h_vol(s->vddig);

  if (s->clkdiv_core == 0)
    {
      bk7258_dvfs_write_cpu0_speed(s->cpu0_speed);
    }

  bk7258_dvfs_write_clkdiv_core(s->clkdiv_core);

  if (s->clkdiv_core != 0)
    {
      bk7258_dvfs_write_cpu0_speed(s->cpu0_speed);
    }

  bk7258_dvfs_undivide_ap();
  bk7258_dvfs_write_cksel_core(s->cksel_core);
  bk7258_dvfs_barrier();
}

/* Going down: mux and dividers first, rails last. */

static void bk7258_dvfs_step_down(const struct bk7258_dvfs_step_s *s)
{
  bk7258_dvfs_write_cksel_core(s->cksel_core);
  bk7258_dvfs_write_clkdiv_core(s->clkdiv_core);
  bk7258_dvfs_write_cpu0_speed(s->cpu0_speed);
  bk7258_dvfs_undivide_ap();
  bk7258_dvfs_barrier();

  sys_hal_ctrl_vdddig_h_vol(s->vddig);
  sys_hal_ctrl_vddd_h_vol(s->vddd);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_dvfs_set_opp(int opp)
{
  int i;

  if (opp < BK7258_OPP_MIN || opp > BK7258_OPP_MAX)
    {
      return -EINVAL;
    }

  if (opp == g_bk7258_dvfs_opp)
    {
      return OK;
    }

  /* Walk one step at a time, like the authority's sys_drv_switch_cpu_bus_freq:
   * a multi-step jump leaves the rails and the dividers out of order.
   */

  if (opp > g_bk7258_dvfs_opp)
    {
      for (i = g_bk7258_dvfs_opp + 1; i <= opp; i++)
        {
          bk7258_dvfs_step_up(&g_bk7258_dvfs_steps[i]);
          g_bk7258_dvfs_opp = i;
        }
    }
  else
    {
      for (i = g_bk7258_dvfs_opp - 1; i >= opp; i--)
        {
          bk7258_dvfs_step_down(&g_bk7258_dvfs_steps[i]);
          g_bk7258_dvfs_opp = i;
        }
    }

  return OK;
}

int bk7258_dvfs_get_opp(void)
{
  return g_bk7258_dvfs_opp;
}

uint32_t bk7258_dvfs_cpu0_hz(void)
{
  return g_bk7258_cpu0_hz[g_bk7258_dvfs_opp];
}

uint32_t bk7258_dvfs_ap_hz(void)
{
  return g_bk7258_ap_hz[g_bk7258_dvfs_opp];
}

uint32_t bk7258_dvfs_bus_hz(void)
{
  return g_bk7258_bus_hz[g_bk7258_dvfs_opp];
}

/* Read the five fields back out of the registers and report what the tree
 * actually holds, so a wrong table entry shows up as a mismatch rather than as
 * a mysteriously slow core.
 */

bool bk7258_dvfs_matches_opp(int opp)
{
  const struct bk7258_dvfs_step_s *s;
  uint32_t m1;
  uint32_t cksel;
  uint32_t clkdiv;
  uint32_t speed0;
  uint32_t speed1;

  if (opp < BK7258_OPP_MIN || opp > BK7258_OPP_MAX)
    {
      return false;
    }

  s = &g_bk7258_dvfs_steps[opp];

  m1      = getreg32(BK7258_SYS_CLK_DIV_MODE1);
  cksel   = (m1 >> 4) & 0x3u;
  clkdiv  = m1 & 0xfu;
  speed0  = (getreg32(BK7258_SYS_CPU0_HALT_CLK_OP) >> 4) & 0x1u;
  speed1  = (getreg32(BK7258_SYS_CPU1_HALT_CLK_OP) >> 4) & 0x1u;

  return cksel == s->cksel_core && clkdiv == s->clkdiv_core &&
         speed0 == s->cpu0_speed && speed1 == 1u;
}

void bk7258_dvfs_report(void)
{
  uint32_t m1 = getreg32(BK7258_SYS_CLK_DIV_MODE1);

  syslog(LOG_INFO,
         "[dvfs] opp=%d cpu0=%lu MHz ap=%lu MHz bus=%lu MHz "
         "m1=%08lx cksel=%lu clkdiv=%lu speed0=%lu speed1=%lu match=%d\n",
         g_bk7258_dvfs_opp,
         (unsigned long)(bk7258_dvfs_cpu0_hz() / 1000000u),
         (unsigned long)(bk7258_dvfs_ap_hz() / 1000000u),
         (unsigned long)(bk7258_dvfs_bus_hz() / 1000000u),
         (unsigned long)m1, (unsigned long)((m1 >> 4) & 0x3u),
         (unsigned long)(m1 & 0xfu),
         (unsigned long)((getreg32(BK7258_SYS_CPU0_HALT_CLK_OP) >> 4) & 0x1u),
         (unsigned long)((getreg32(BK7258_SYS_CPU1_HALT_CLK_OP) >> 4) & 0x1u),
         (int)bk7258_dvfs_matches_opp(g_bk7258_dvfs_opp));
}

#endif /* CONFIG_BK7258_COMPONENT_CP */

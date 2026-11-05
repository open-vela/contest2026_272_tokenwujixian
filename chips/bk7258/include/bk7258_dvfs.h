/****************************************************************************
 * chips/bk7258/include/bk7258_dvfs.h
 *
 * Shared CPU/bus operating points.  Derived from the field-proven tables in
 * contest2026_135_yongwangzhiqian (chips/bk7258/common/bk7258_dvfs.h, its own
 * Apache-2.0 NuttX rewrite of the Armino runtime clock path) and
 * contest2026_264_VelaSight (Armino source driven from pwr_clk.c).  Both hold
 * that the SoC has one shared operating point: the AP cannot be clocked on its
 * own, and 320M/480M are not CPU0 frequencies.
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_DVFS_H
#define __VENDOR_BEKEN_CHIP_BK7258_DVFS_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <arch/chip/bk7258_memorymap.h>

/* Operating points, in the SDK's policy order.  The order is part of the ABI:
 * AP 320/480 requests depend on OPP 5/6 staying the highest votes.
 */

#define BK7258_OPP_26M        0
#define BK7258_OPP_60M        1
#define BK7258_OPP_80M        2
#define BK7258_OPP_120M       3
#define BK7258_OPP_240M       4
#define BK7258_OPP_320M       5
#define BK7258_OPP_480M       6

#define BK7258_OPP_MIN        BK7258_OPP_26M
#define BK7258_OPP_MAX        BK7258_OPP_480M

/* The two relationships that make 320M/480M what they are, as scalars so they
 * can be checked at compile time (a brace-initializer macro cannot be indexed
 * in a static assertion).
 */

#define BK7258_OPP_CPU0_240M_HZ  240000000u
#define BK7258_OPP_CPU0_320M_HZ  160000000u
#define BK7258_OPP_CPU0_480M_HZ  240000000u
#define BK7258_OPP_AP_480M_HZ    480000000u

/* Documented frequencies per operating point.  These are the values the static
 * assertions in bk7258_dvfs.c check the register table against.
 */

#define BK7258_OPP_CPU0_HZ                                           \
  { 26000000u, 60000000u, 80000000u, 120000000u,                     \
    240000000u, 160000000u, 240000000u }

#define BK7258_OPP_AP_HZ                                             \
  { 26000000u, 60000000u, 80000000u, 120000000u,                     \
    240000000u, 320000000u, 480000000u }

#define BK7258_OPP_BUS_HZ                                            \
  { 26000000u, 60000000u, 80000000u, 120000000u,                     \
    240000000u, 160000000u, 240000000u }

/* SYS registers the operating point is spread across.  Word-indexed: the SYNC
 * block's reg0x08 is byte offset 0x20, not 0x08.
 */

#define BK7258_SYS_CLK_DIV_MODE1        (BK7258_SYS_BASE + 0x20u)
#define BK7258_SYS_CPU0_HALT_CLK_OP     (BK7258_SYS_BASE + 0x10u)
#define BK7258_SYS_CPU1_HALT_CLK_OP     (BK7258_SYS_BASE + 0x14u)

#ifdef CONFIG_BK7258_COMPONENT_CP

/* Move to `opp`, stepping through every point in between.  Must run from the CP
 * and before CPU1 is released: the register that carries CPU1's speed bit also
 * carries its entry offset, so only the CP may write it.
 */

int bk7258_dvfs_set_opp(int opp);
int bk7258_dvfs_get_opp(void);

uint32_t bk7258_dvfs_cpu0_hz(void);
uint32_t bk7258_dvfs_ap_hz(void);
uint32_t bk7258_dvfs_bus_hz(void);

/* True when the registers hold exactly the fields the table claims. */

bool bk7258_dvfs_matches_opp(int opp);

/* Log the operating point, the register readback and the match result. */

void bk7258_dvfs_report(void);

#endif /* CONFIG_BK7258_COMPONENT_CP */

#endif /* __VENDOR_BEKEN_CHIP_BK7258_DVFS_H */

/*
 * chips/bk7258/wifi/hal_port/include/bk_sys_ctrl.h
 *
 * System-control command codes and analog trim masks consumed by the vendored
 * bk_phy_adapter.c, which both passes the CMD_* codes to ddev_control() and puts
 * the masks in the PHY value table (lines 772-773).
 *
 * ==== Analog trim masks: SoC branch matters ====
 *
 * Upstream defines these three times, gated on SoC family
 * (bk_private/bk_sys_ctrl.h:313-325):
 *
 *   BK7231N / BK7236A / BK7256XX     XTALH_CTUNE 0x7F
 *   BK7236XX / BK7239XX / BK7286XX   XTALH_CTUNE 0xFF   <- BK7258 is here
 *   everything else except BK7231    XTALH_CTUNE 0x3F
 *
 * Picking a neighbouring branch compiles and links cleanly, then halves the
 * crystal fine-tune range and skews RF frequency accuracy. AUD_DAC_GAIN_MASK is
 * 0x1F in all three; stated explicitly so that is visibly a fact, not an
 * assumption.
 *
 * ==== CMD_* codes: positional inside a magic-anchored enum ====
 *
 * The commands are enum members anchored at SCTRL_CMD_MAGIC (0xC123000), so their
 * values fall out of the member order rather than being written down. The enum
 * was walked to derive the ones used here; they are spelled as explicit constants
 * because a trimmed enum would renumber silently.
 */

#ifndef __BK7258_WIFI_GLUE_BK_SYS_CTRL_H
#define __BK7258_WIFI_GLUE_BK_SYS_CTRL_H

#include <common/bk_typedef.h>

/* bk_phy_adapter.c uses CMD_TL410_CLK_PWR_UP and PWD_BLE_CLK_BIT without
 * including bk_icu.h itself, so upstream must be supplying them transitively --
 * its own bk_sys_ctrl.h pulls in "pmu.h", which we do not vendor. Hooking
 * bk_icu.h in here reproduces that reachability through a chain we own.
 */

#include "bk_icu.h"

#define SCTRL_CMD_MAGIC             (0xC123000)
#define SCTRL_FAILURE               ((UINT32)-1)
#define SCTRL_SUCCESS               (0)

/* Analog trim masks, BK7236XX family. */

#define PARAM_XTALH_CTUNE_MASK      (0xFF)
#define PARAM_AUD_DAC_GAIN_MASK     (0x1F)

/* ddev_control command codes, derived by walking the authority SCTRL enum in
 * cp/middleware/driver/include/bk_private/bk_sys_ctrl.h.
 *
 * THE TRAP (fixed 2026-09-04): that enum carries seven `#if` blocks keyed on
 * CONFIG_SOC_*, so the ordinal of every member after the first block depends on
 * the SOC.  Walking it WITHOUT applying the preprocessor yields 115 members;
 * BK7258 (CONFIG_SOC_BK7258=1, CONFIG_SOC_BK7236XX=1, and BK7271 / BK7231 /
 * BK7231N / BK7236A / BK7256XX / BK7251 all undefined) activates only 89.
 *
 * The four values below used to be the unconditional-walk results, i.e. they
 * named a DIFFERENT command than the authority does -- +6 for the BLE_RF pair
 * and +25 for the VDD pair.  These are command IDs handed to the closed
 * library through phy_os_variable slots 27..30, so a wrong ordinal makes the
 * library request some other operation entirely.
 *
 * The first four are ahead of the first `#if` and were therefore already
 * correct; they are kept to document that this was verified, not assumed.
 * The two VDD values are independently corroborated by the cross-boundary ABI
 * audit (/tmp/other-contract-behavior-audit.tsv rows 29/30, authority
 * 0x0c123058 / 0x0c123057).
 *
 * Re-derive with the preprocessor applied, never by counting lines.
 */

#define CMD_GET_CHIP_ID             (0x0C123001)  /* magic +1  */
#define CMD_GET_DEVICE_ID           (0x0C123002)  /* magic +2  */
#define CMD_SCTRL_BLE_POWERDOWN     (0x0C12301B)  /* magic +27 */
#define CMD_SCTRL_BLE_POWERUP       (0x0C12301C)  /* magic +28 */
#define CMD_BLE_RF_BIT_SET          (0x0C12302E)  /* magic +46 */
#define CMD_BLE_RF_BIT_CLR          (0x0C12302F)  /* magic +47 */
#define CMD_SCTRL_SET_VDD_VALUE     (0x0C123057)  /* magic +87 */
#define CMD_SCTRL_GET_VDD_VALUE     (0x0C123058)  /* magic +88 */

#define SYS_DRV_CLK_ON              1
#define SYS_DRV_CLK_OFF             0
/* BK7258 authority sys_types.h: low 16 bits are stepping data; PM/PHY
 * revision comparisons intentionally use only the high product/revision
 * field.  This value is exported through g_phy_os_variable. */
#define PM_CHIP_ID_MASK             0xFFFF0000u
#define PM_CHIP_ID_MPW_V2_3         0x22710010u
#define PM_CHIP_ID_MPW_V4           0x22C20010u
#define PM_CHIP_ID_MP_A             0x23640810u
#define SARADC_AUTOTEST             0

#endif /* __BK7258_WIFI_GLUE_BK_SYS_CTRL_H */

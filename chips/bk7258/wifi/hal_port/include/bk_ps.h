/*
 * chips/bk7258/wifi/hal_port/include/bk_ps.h
 *
 * Power-save command codes used by the vendored glue. Upstream this header
 * belongs to the bk_ps component, which we do not vendor.
 *
 * NOT an empty compatibility header, despite Wi-Fi power save being out of STA
 * MVP scope: wifi_v2.c passes six of these codes to the vendor runtime through
 * bmsg, so the numeric values are part of the interface and must match
 * upstream exactly. Upstream splits them across two enums whose names both
 * start with PS_BMSG_IOCTL_, and wifi_v2.c uses members of both.
 *
 * Only the enums are reproduced. The power_save_* / mac_ps_* functions that
 * upstream also declares here are not: rw_task.c declares the two it uses
 * itself (lines 92-93) and they arrive through g_wifi_funcs.
 */

#ifndef __BK7258_WIFI_GLUE_BK_PS_H
#define __BK7258_WIFI_GLUE_BK_PS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Values as upstream bk_ps.h:23-44. Explicitly numbered because they cross
 * the hal_port/firmware boundary.
 */

typedef enum
{
  PS_BMSG_IOCTL_RF_ENABLE         = 0,
  PS_BMSG_IOCTL_RF_DISANABLE      = 1,
  PS_BMSG_IOCTL_MCU_ENABLE        = 2,
  PS_BMSG_IOCTL_MCU_DISANABLE     = 3,
  PS_BMSG_IOCTL_RF_USER_WKUP      = 4,
  PS_BMSG_IOCTL_RF_KP_SET         = 5,
  PS_BMSG_IOCTL_RF_TD_SET         = 6,
  PS_BMSG_IOCTL_RF_KP_HANDLER     = 7,
  PS_BMSG_IOCTL_RF_TD_HANDLER     = 8,
  PS_BMSG_IOCTL_RF_KP_STOP        = 9,
  PS_BMSG_IOCTL_WAIT_TM_SET       = 10,
  PS_BMSG_IOCTL_WAIT_TM_HANDLER   = 11,
  PS_BMSG_IOCTL_RF_PS_TIMER_INIT  = 12,
  PS_BMSG_IOCTL_RF_PS_TIMER_DEINIT = 13,
  PS_BMSG_IOCTL_AP_PS_STOP        = 14,
  PS_BMSG_IOCTL_AP_PS_START       = 15,
  PS_BMSG_IOCTL_AP_PS_RUN         = 16
} PS_BMSG_IOCTL_CMD;

/* Second enum, upstream bk_ps.h:64-69 (commented there as "use for BK7256").
 * Separate numbering space; wifi_v2.c uses PS_ENABLE / PS_DISANABLE from here.
 */

typedef enum
{
  PS_BMSG_IOCTL_PS_ENABLE      = 0,
  PS_BMSG_IOCTL_PS_DISANABLE,
  PS_BMSG_IOCTL_EXC32K_START,
  PS_BMSG_IOCTL_EXC32K_STOP
} PS_CMD;

/* Power-save forbid reasons, upstream bk_ps.h:45-54. Five of these go into the
 * vendor value table (bk_wifi_adapter.c:1486-1490), so the numbering is part of
 * the interface -- libwifi.a compares against these codes when reporting why it
 * cannot sleep. Note the enum starts at 1, not 0.
 */

typedef enum
{
  PS_FORBID_NO_ON       = 1,
  PS_FORBID_PREVENT     = 2,
  PS_FORBID_VIF_PREVENT = 3,
  PS_FORBID_IN_DOZE     = 4,
  PS_FORBID_KEEVT_ON    = 5,
  PS_FORBID_BMSG_ON     = 6,
  PS_FORBID_TXING       = 7,
  PS_FORBID_HW_TIMER    = 8,
  PS_FORBID_RXING       = 9
} PS_FORBID_STATUS;

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BK_PS_H */

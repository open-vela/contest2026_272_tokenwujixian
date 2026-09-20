/*
 * chips/bk7258/wifi/hal_port/include/drv_model_pub.h
 *
 * Armino's legacy driver-model handles. bk_phy_adapter.c reaches the SCTRL / ICU
 * / BLE blocks through ddev_open()+ddev_control() rather than a typed API, and
 * puts the device-type ids in the PHY value table.
 *
 * The ids are positional inside one long enum anchored at
 * DD_HANDLE_MAGIC_WORD - 1 (0xA5A4FFFF), so they cannot be read off individual
 * lines. The whole 38-member enum was walked to derive the ones used here:
 *
 *     DD_DEV_TYPE_BLE     0xA5A50001
 *     DD_DEV_TYPE_ICU     0xA5A50006
 *     DD_DEV_TYPE_SCTRL   0xA5A5000E
 *     DD_DEV_TYPE_FLASH   0xA5A50015
 *     DD_DEV_TYPE_SARADC  0xA5A50021
 *     DD_DEV_TYPE_RF      0xA5A50022
 *
 * They are written as explicit constants, not as a trimmed enum: an enum missing
 * members would renumber silently, which is the whole hazard here.
 *
 * ddev_open/close/read/write are already declared in bk_drv_model.h; only
 * ddev_control is added, since that is what bk_phy_adapter.c uses. The
 * implementation is in hal_port/analog_shim.c and fails rather than pretending.
 */

#ifndef __BK7258_WIFI_GLUE_DRV_MODEL_PUB_H
#define __BK7258_WIFI_GLUE_DRV_MODEL_PUB_H

#include <stdint.h>

#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DD_HANDLE_MAGIC_WORD    (0xA5A50000)

/* ICU and SCTRL corrected 2026-09-04; see the per-line notes below.
 *
 * Root cause of the drift: the enum in authority
 * cp/middleware/driver/include/bk_private/legacy/drv_model_pub.h carries ~7
 * `#if` blocks keyed on CONFIG_BLUETOOTH / CONFIG_FFT / CONFIG_GENERAL_DMA /
 * CONFIG_I2S / CONFIG_QSPI / CONFIG_MAC_PHY_BYPASS / CONFIG_SOC_*, so a member's
 * ordinal depends on the build configuration.  Walking it as if every member
 * were present -- which is what produced the old values -- shifts everything
 * after the first excluded block.  Same mistake as the CMD_SCTRL_* block in
 * bk_sys_ctrl.h and the OTP ids in driver/otp.h.
 *
 * Evidence rule applied here: a value is only changed when BOTH the source walk
 * (with the authority sdkconfig.h applied) AND the cross-boundary ABI audit
 * (authority app.elf object bytes, /tmp/other-contract-behavior-audit.tsv)
 * agree.  That holds for ICU and SCTRL and for nothing else in this list.
 */

#define DD_DEV_TYPE_NONE        (0x00000000)

/* BLE / FLASH: left alone deliberately.  The ABI audit reports both as already
 * byte-identical to authority (TSV rows 12 and 15).  A source walk here claimed
 * they are excluded by `#if` on this configuration, which cannot be right --
 * bk_phy_adapter.c assigns `._dd_dev_type_ble = DD_DEV_TYPE_BLE`, so authority
 * would not compile if the enumerator were absent.  Measured bytes beat a
 * source walk; do not "fix" these without new evidence. */

#define DD_DEV_TYPE_BLE         (0xA5A50001)
#define DD_DEV_TYPE_FLASH       (0xA5A50015)

/* ICU / SCTRL: source walk and ABI audit agree on both, and both disagreed with
 * the old values (0xA5A50006 / 0xA5A5000E). */

#define DD_DEV_TYPE_ICU         (0xA5A50003)
#define DD_DEV_TYPE_SCTRL       (0xA5A50007)

/* SARADC / RF: UNVERIFIED, left at the original walk results.  phy_os_variable_t
 * has no slots for these two, so the ABI audit offers no measurement either way,
 * and the source walk alone has just been shown unreliable for this enum.  They
 * are consumed only through ddev_control(), which the board logs prove is never
 * called (hal_port/analog_shim.c ANALOG_UNPORTED never fires -- zero `[bk_analog]`
 * lines across 0904-open-1/open-2/wap-2), so a wrong value here has no runtime
 * effect today.  Re-derive from a disassembly of the authority ddev_open call
 * sites before relying on them. */

#define DD_DEV_TYPE_SARADC      (0xA5A50021)
#define DD_DEV_TYPE_RF          (0xA5A50022)

typedef UINT32 DD_HANDLE;
typedef UINT32 dd_device_type;

DD_HANDLE ddev_open(dd_device_type dev, UINT32 *status, UINT32 op_flag);
UINT32    ddev_close(DD_HANDLE handle);
UINT32    ddev_read(DD_HANDLE handle, char *user_buf, UINT32 count,
                    UINT32 op_flag);
UINT32    ddev_write(DD_HANDLE handle, char *user_buf, UINT32 count,
                     UINT32 op_flag);
UINT32    ddev_control(DD_HANDLE handle, UINT32 cmd, void *param);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRV_MODEL_PUB_H */

/* hal_port_bandgap.c - boot-time bandgap trim, ported verbatim from the
 * authoritative Armino source so the analog reference matches the reference
 * environment.
 *
 * Source (function body copied, only the wrapper renamed):
 *   cp/components/bk_init/components_init.c:97  bandgap_init()
 *
 * Why this is a real gap rather than a cosmetic one.  bandgap_init() is step
 * three of the authority's components_init() sequence -- driver_early_init(),
 * pm_init(), bandgap_init(), random_init(), bk_stack_guard_setup() -- and it
 * is the only one of those that programs an analog trim.  It reads the
 * per-die VDDDIG bandgap calibration byte out of OTP and writes it to the
 * bandgap trim field, so the analog bandgap sits at its calibrated absolute
 * value rather than at whatever reset left in the register.
 *
 * This port never ran that step.  Until 2026-09-09 that was invisible because
 * sys_drv_set_bgcalm() was a log-only stub in hal_port/analog_shim.c: the PHY's
 * own bandgap writes went nowhere either, so the register simply kept its
 * reset value throughout.  Making sys_drv_set_bgcalm() real without also
 * running this init produced a worse state than the stub -- the PHY can now
 * move the trim, but from an uncalibrated starting point.  That is the same
 * mistake as porting sys_hal_exit_low_analog() without porting whatever
 * establishes ana_reg11.aldosel: aligning a leaf while leaving the code that
 * establishes its input unaligned.
 *
 * The bandgap is the reference for every analog block on the die, the RF PLL
 * included, so an uncalibrated reference is a plausible cause of a channel
 * hop that cannot re-lock.  Stated as the reason this step was prioritised,
 * not as a diagnosis -- it has not been confirmed against hardware.
 *
 * Preconditions, all verified before writing this file:
 *   - CONFIG_SOC_BK7236XX and CONFIG_OTP_V1 are both 1 here
 *     (hal_port/include/common/sys_config.h:21,101), matching the authority
 *     BK7258 CP config, so the guarded body below is live and not compiled
 *     out.
 *   - sys_drv_get_bgcalm()/sys_drv_set_bgcalm() reach the verbatim register
 *     sequence in hal_port_lpdoze.c (sys_hal.c:1938,1943) instead of the
 *     former stubs.
 *   - bk_otp_apb_read() is the imported authority driver
 *     (otp/armino/.../otp_driver_v1_1.c:288) and does not require
 *     bk_otp_driver_init(): it brackets its own access with OTP_ACTIVE()/
 *     OTP_SLEEP().  Confirmed on hardware -- libbk_phy.a reads the die
 *     temperature through the same entry point and logs a sane value
 *     ("[cal] temp in otp is:563") on every boot captured so far.
 */

#include <stdint.h>
#include <nuttx/config.h>

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <components/log.h>

/* driver/_otp.h carries the OTP item enum (OTP_VDDDIG_BANDGAP:83,
 * OTP_DEVICE_ID:87) but declares no functions; driver/otp.h:18 declares
 * bk_otp_apb_read(); sys_driver.h:50-51 declares the bgcalm accessors.  All
 * three are needed and none of them pulls in the others. */
#include <driver/_otp.h>
#include <driver/otp.h>
#include "sys_driver.h"

#define TAG "init"

/* components_init.c:97, verbatim apart from the hp_ prefix. */

int hp_bandgap_init(void)
{
#if (CONFIG_SOC_BK7236XX) && (CONFIG_OTP_V1)
	uint8_t device_id[2]; //only check seqNUM
	uint8_t new_bandgap;
	uint8_t old_bandgap;
	bk_err_t result;

	old_bandgap = (uint8_t)sys_drv_get_bgcalm();

	result = bk_otp_apb_read(OTP_VDDDIG_BANDGAP, &new_bandgap, sizeof(new_bandgap));
	if ((result != BK_OK) || (new_bandgap == 0) || (new_bandgap > 0x3F)) {
		goto default_bandgap;
	}

	result = bk_otp_apb_read(OTP_DEVICE_ID, device_id, sizeof(device_id));
	if ((result != BK_OK) || ((device_id[0] == 0x32) && (device_id[1] == 0x31))) {
		goto default_bandgap;
	}

	BK_LOGD(TAG, "bandgap_calm_in_otp=0x%x\r\n", new_bandgap);
	if (old_bandgap != new_bandgap) {
		sys_drv_set_bgcalm(new_bandgap);
	}
	return BK_OK;

default_bandgap:
	//tenglong20240717: increase 10mV as default
	if (old_bandgap > 6) {
		sys_drv_set_bgcalm(old_bandgap - 6);
	}
#endif
	return BK_OK;
}

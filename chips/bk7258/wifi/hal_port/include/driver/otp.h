/*
 * BK7258 OTP API bridge for the vendored PHY adapter.
 *
 * The complete Armino BK7258 OTP driver closure owns item enums, map layout,
 * privilege values and read/update behavior. Do not add local IDs or an
 * alternate bk_otp_* implementation here.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_OTP_H
#define __BK7258_WIFI_GLUE_DRIVER_OTP_H

#include <driver/otp_types.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_otp_apb_read(otp1_id_t item, uint8_t *buf, uint32_t size);
bk_err_t bk_otp_apb_update(otp1_id_t item, uint8_t *buf, uint32_t size);
bk_err_t bk_otp_ahb_read(otp2_id_t item, uint8_t *buf, uint32_t size);
bk_err_t bk_otp_ahb_update(otp2_id_t item, uint8_t *buf, uint32_t size);
otp_privilege_t bk_otp_apb_read_permission(otp1_id_t item);
otp_privilege_t bk_otp_ahb_read_permission(otp2_id_t item);
bk_err_t bk_otp_apb_write_permission(otp1_id_t item, otp_privilege_t permission);
bk_err_t bk_otp_ahb_write_permission(otp2_id_t item, otp_privilege_t permission);
otp_privilege_t bk_otp_apb_read_mask(otp1_id_t item);
otp_privilege_t bk_otp_ahb_read_mask(otp2_id_t item);
bk_err_t bk_otp_apb_write_mask(otp1_id_t item, uint32_t mask);
bk_err_t bk_otp_ahb_write_mask(otp2_id_t item, uint32_t mask);
bk_err_t bk_otp_apb_read_by_offset(uint32_t item_offset, uint8_t *buf,
                                    uint32_t size);
bk_err_t bk_otp_read_random_number(uint32_t *value, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_OTP_H */

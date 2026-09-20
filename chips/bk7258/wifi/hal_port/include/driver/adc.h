/*
 * chips/bk7258/wifi/hal_port/include/driver/adc.h
 *
 * SARADC access used by the vendored bk_phy_adapter.c for RF calibration:
 * bk_cal_saradc_start() / bk_cal_saradc_stop() (lines 418-450) acquire the ADC,
 * push an adc_config_t, and start sampling. Those calls are live -- they sit
 * outside the SARADC_AUTOTEST block that gates the other SARADC path.
 *
 * bk_adc_set_phy_cali_config() is declared here too. Upstream keeps it in
 * middleware/driver/saradc/adc_driver.h rather than this header; collecting
 * both here keeps one declaration site, and adc_driver.h forwards to this file.
 *
 * Implementations use the BK7258 SARADC MMIO block in hal_port/bk7258_saradc.c.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_ADC_H
#define __BK7258_WIFI_GLUE_DRIVER_ADC_H

#include <stdint.h>

#include <common/bk_err.h>

#include "driver/hal/hal_adc_types.h"

#define BK_ERR_ADC_NOT_INIT           (BK_ERR_ADC_BASE - 1)
#define BK_ERR_ADC_INVALID_CHAN       (BK_ERR_ADC_BASE - 2)
#define BK_ERR_ADC_BUSY               (BK_ERR_ADC_BASE - 3)
#define BK_ERR_ADC_INVALID_MODE       (BK_ERR_ADC_BASE - 4)
#define BK_ERR_ADC_CHAN_NOT_INIT      (BK_ERR_ADC_BASE - 5)
#define BK_ERR_ADC_INVALID_SCLK_MODE  (BK_ERR_ADC_BASE - 6)
#define BK_ERR_ADC_GET_READ_SEMA      (BK_ERR_ADC_BASE - 8)

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_adc_acquire(void);
bk_err_t bk_adc_release(void);
bk_err_t bk_adc_init(adc_chan_t adc_id);
bk_err_t bk_adc_deinit(adc_chan_t id);
bk_err_t bk_adc_start(void);
bk_err_t bk_adc_stop(void);
bk_err_t bk_adc_read_raw(uint16_t *read_buf, uint32_t size, uint32_t timeout);
bk_err_t bk_adc_enable_bypass_clalibration(void);
bk_err_t bk_adc_set_config(adc_config_t *config);
bk_err_t bk_adc_set_phy_cali_config(adc_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_ADC_H */

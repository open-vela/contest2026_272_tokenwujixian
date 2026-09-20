/*
 * chips/bk7258/wifi/hal_port/include/temp_detect_pub.h
 *
 * Temperature / voltage monitoring entry points. bk_phy_adapter.c registers all
 * of these into the PHY function table (lines 541-548), so libbk_phy.a queries
 * chip temperature and core voltage to compensate RF output power.
 *
 * Signatures are upstream's (components/temp_detect/temp_detect_pub.h:42-52).
 * The tuning constants live in temp_detect.h, included here as upstream does.
 *
 * Implementations are in hal_port/analog_shim.c. BK7258 samples the on-chip sensor
 * through SARADC channel 7 and returns the vendor PHY's processed ADC code (not
 * degrees Celsius). The periodic worker invokes the pinned PHY calibration
 * provider below only after a successful sample.
 */

#ifndef __BK7258_WIFI_GLUE_TEMP_DETECT_PUB_H
#define __BK7258_WIFI_GLUE_TEMP_DETECT_PUB_H

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_typedef.h>

#include "temp_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

int  temp_detect_init(uint32_t init_val);
int  temp_detect_deinit(void);
bool temp_detect_is_init(void);
int  temp_detect_get_temperature(uint32_t *temperature);
int  volt_single_get_current_voltage(UINT32 *volt_value);
int  volt_detect_start(void);
int  volt_detect_stop(void);

/* Pinned libbk_phy.a calibration ABI. This is the exact declaration used by
 * the BK7258 Armino temperature daemon. It is intentionally not a shim or a
 * weak fallback: builds that enable the vendor Wi-Fi runtime must link the
 * provider and retain this symbol in the final ELF. */

void rwnx_cal_do_temp_detect(uint16_t cur_val, uint16_t threshold,
                             uint16_t *last);
void rwnx_cal_do_volt_detect(uint16_t volt_adc);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_TEMP_DETECT_PUB_H */

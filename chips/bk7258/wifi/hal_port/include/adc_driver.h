/*
 * chips/bk7258/wifi/hal_port/include/adc_driver.h
 *
 * Bare-name forwarder to driver/adc.h, which carries the SARADC declarations.
 *
 * Previously an empty compatibility header, on the grounds that bk_rf_adapter.c
 * includes it without using any ADC symbol. That much is true, but upstream also
 * declares bk_adc_set_phy_cali_config() here, and bk_phy_adapter.c:437 calls it.
 * Forwarding keeps both include spellings working from one declaration site.
 */

#include "driver/adc.h"

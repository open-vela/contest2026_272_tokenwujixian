/* Authority iot_adc.h subset required by adc_driver.c. */

#ifndef __BK7258_AUTHORITY_NUTTX_IOT_ADC_H
#define __BK7258_AUTHORITY_NUTTX_IOT_ADC_H

#include <stdint.h>

typedef void (*IotAdcCallback_t)(uint16_t *converted_data, void *context);

#endif /* __BK7258_AUTHORITY_NUTTX_IOT_ADC_H */

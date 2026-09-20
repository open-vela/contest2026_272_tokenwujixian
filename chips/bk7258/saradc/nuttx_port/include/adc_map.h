/* Authority BK7258 ADC map projected onto the NuttX GPIO boundary. */

#ifndef __BK7258_AUTHORITY_NUTTX_ADC_MAP_H
#define __BK7258_AUTHORITY_NUTTX_ADC_MAP_H

#include <driver/hal/hal_adc_types.h>

typedef struct
{
  adc_chan_t adc_chan;
  unsigned int gpio_id;
  unsigned int gpio_dev;
} adc_gpio_map_t;

#define ADC_DEV_MAP \
{ \
  {ADC_0,  0, 0}, {ADC_1, 25, 0}, {ADC_2, 24, 0}, {ADC_3, 23, 0}, \
  {ADC_4, 28, 0}, {ADC_5, 22, 0}, {ADC_6, 21, 0}, {ADC_7,  0, 0}, \
  {ADC_8,  0, 0}, {ADC_9,  0, 0}, {ADC_10, 8, 0}, {ADC_11, 0, 0}, \
  {ADC_12, 0, 0}, {ADC_13, 1, 0}, {ADC_14,12, 0}, {ADC_15,13, 0}, \
}

#endif /* __BK7258_AUTHORITY_NUTTX_ADC_MAP_H */

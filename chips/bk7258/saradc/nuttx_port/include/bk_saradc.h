/* Authority BK7258 SARADC public types retained at the NuttX boundary. */

#ifndef __BK7258_AUTHORITY_NUTTX_BK_SARADC_H
#define __BK7258_AUTHORITY_NUTTX_BK_SARADC_H

#include <common/bk_typedef.h>

#define SARADC_FAILURE 1
#define SARADC_SUCCESS 0

#define ADC_CONFIG_MODE_SLEEP       (0x00UL)
#define ADC_CONFIG_MODE_STEP        (0x01UL)
#define ADC_CONFIG_MODE_SOFT_CTRL   (0x02UL)
#define ADC_CONFIG_MODE_CONTINUE    (0x03UL)
#define ADC_CONFIG_MODE_4CLK_DELAY  (0x0UL)
#define ADC_CONFIG_MODE_8CLK_DELAY  (0x1UL)
#define ADC_CONFIG_MODE_SHOULD_OFF  (1 << 3)

typedef enum
{
  SARADC_CALIBRATE_LOW,
  SARADC_CALIBRATE_HIGH
} SARADC_MODE;

/* Verbatim authority descriptor layout (bk_private/bk_saradc.h). */
typedef struct
{
  UINT16 *pData;
  volatile UINT8 current_sample_data_cnt;
  volatile UINT8 current_read_data_cnt;
  UINT8 data_buff_size;
  volatile UINT8 has_data;
  volatile UINT8 all_done;
  UINT8 channel;
  UINT8 mode;
  void (*p_Int_Handler)(void);
  unsigned char pre_div;
  unsigned char samp_rate;
  unsigned char filter;
} saradc_desc_t;

typedef void (*adc_isr_t)(uint32_t param);

#define BK_ERR_ADC_INIT_MUTEX       (BK_ERR_ADC_BASE - 7)
#define BK_ERR_ADC_INIT_READ_SEMA   (BK_ERR_ADC_BASE - 9)
#define BK_ERR_ADC_SIZE_TOO_BIG     (BK_ERR_ADC_BASE - 10)
#define SOC_ADC_SAMPLE_CNT_MAX      32

/* Values from authority bk_sys_ctrl.h for the BK7258 SoC branch. */
#define PARAM_SARADC_BT_TXSEL_BIT   (UINT32_C(1) << 5)
#define BLK_BIT_SARADC              (UINT32_C(1) << 13)
#define CMD_SCTRL_BLK_ENABLE        UINT32_C(0x0c123015)

typedef struct _saradc_calibrate_val_
{
  unsigned short low;
  unsigned short high;
} saradc_calibrate_val;

extern saradc_calibrate_val saradc_val;
extern UINT8 g_saradc_flag;

UINT32 saradc_set_calibrate_val(uint16_t *value, SARADC_MODE mode);
float saradc_calculate(UINT16 adc_val);

#endif /* __BK7258_AUTHORITY_NUTTX_BK_SARADC_H */

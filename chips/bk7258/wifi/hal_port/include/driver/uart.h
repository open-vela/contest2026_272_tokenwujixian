/* Minimal Armino UART compatibility contract for the Wi-Fi provider. */
#ifndef __BK7258_WIFI_GLUE_DRIVER_UART_H
#define __BK7258_WIFI_GLUE_DRIVER_UART_H

#include <common/bk_err.h>

typedef enum
{
  UART_ID_0 = 0,
  UART_ID_1 = 1,
  UART_ID_2 = 2,
  UART_ID_MAX
} uart_id_t;

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t uart_write_string(uart_id_t id, const char *string);

#ifdef __cplusplus
}
#endif

#endif

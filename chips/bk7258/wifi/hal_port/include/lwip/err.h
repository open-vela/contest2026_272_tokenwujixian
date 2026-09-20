/*
 * chips/bk7258/wifi/hal_port/include/lwip/err.h
 *
 * lwIP error type/codes used by the vendored glue (pbuf_copy returns err_t).
 * Written for NuttX rather than vendored; values match lwIP so glue
 * comparisons against ERR_OK / ERR_MEM keep their meaning.
 */

#ifndef __BK7258_WIFI_GLUE_LWIP_ERR_H
#define __BK7258_WIFI_GLUE_LWIP_ERR_H

#include "lwip/arch.h"

typedef s8_t err_t;

#define ERR_OK           0
#define ERR_MEM         -1
#define ERR_BUF         -2
#define ERR_TIMEOUT     -3
#define ERR_RTE         -4
#define ERR_INPROGRESS  -5
#define ERR_VAL         -6
#define ERR_WOULDBLOCK  -7
#define ERR_USE         -8
#define ERR_ALREADY     -9
#define ERR_ISCONN     -10
#define ERR_CONN       -11
#define ERR_IF         -12
#define ERR_ABRT       -13
#define ERR_RST        -14
#define ERR_CLSD       -15
#define ERR_ARG        -16

#endif /* __BK7258_WIFI_GLUE_LWIP_ERR_H */

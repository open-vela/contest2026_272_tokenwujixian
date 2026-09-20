/*
 * chips/bk7258/wifi/hal_port/include/driver/int_types.h
 *
 * ICU interrupt source ids. These are NOT NVIC numbers: bk_wifi_adapter.c
 * stores them in the vendor value table (`._int_src_modem = INT_SRC_MODEM` and
 * friends, lines 1543-1549), so libwifi.a receives them as-is. Our
 * chips/bk7258/include/irq.h holds the NVIC equivalents (= 16 + ICU id); mixing
 * the two would hand the prebuilt numbers that are off by 16, with no failure
 * signal.
 *
 * Upstream ships four variants of this enum, selected by SoC
 * (include/driver/int_types.h): RISC-V, BK7236XX/BK7286XX, BK7239XX, and a
 * default. BK7258 takes the BK7236XX branch (:97-167) -- CONFIG_SOC_BK7236XX is
 * 1 in the prebuilt baseline. The wrong branch would shift every id.
 *
 * The whole enum is reproduced verbatim rather than trimmed to the Wi-Fi
 * members: the ids are positional, so dropping earlier members would silently
 * renumber the ones we care about. The two `= INT_SRC_x` aliases matter too --
 * they do not advance the counter, so removing them shifts everything after.
 *
 * Cross-check: the resulting Wi-Fi ids (MODEM 29, MODEM_RC 30,
 * MAC_TXRX_TIMER 31, MAC_TXRX_MISC 32, MAC_RX_TRIGGER 33, MAC_TX_TRIGGER 34,
 * MAC_PROT_TRIGGER 35, MAC_GENERAL 36, MAC_HSU 37, MAC_WAKEUP 38) agree with
 * upstream's independent icu_map.h table, which lists the same numbers against
 * these sources.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_INT_TYPES_H
#define __BK7258_WIFI_GLUE_DRIVER_INT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  INT_SRC_DMA0_NSEC = 0,
  INT_SRC_ENC_SEC,
  INT_SRC_ENC_NSEC,
  INT_SRC_TIMER,
  INT_SRC_UART0,
  INT_SRC_PWM,
  INT_SRC_I2C0,
  INT_SRC_SPI0,
  INT_SRC_SARADC,
  INT_SRC_IRDA,
  INT_SRC_SDIO,
  INT_SRC_GDMA,
  INT_SRC_LA,
  INT_SRC_TIMER1,
  INT_SRC_I2C1,
  INT_SRC_UART1,
  INT_SRC_UART2,
  INT_SRC_SPI1,
  INT_SRC_CAN,
  INT_SRC_USB,
  INT_SRC_QSPI0,
  INT_SRC_FFT,
  INT_SRC_CKMN = INT_SRC_FFT,       /* bk7236 removed the FFT block */
  INT_SRC_SBC,
  INT_SRC_AUDIO,
  INT_SRC_I2S0,
  INT_SRC_JPEG,
  INT_SRC_JPEG_ENC = INT_SRC_JPEG,
  INT_SRC_JPEG_DEC,
  INT_SRC_LCD,
  INT_SRC_DMA2D,
  INT_SRC_MODEM,                    /* 29, phy_mbp_int */
  INT_SRC_MODEM_RC,                 /* 30, phy_riu_int */
  INT_SRC_MAC_TXRX_TIMER,           /* 31 */
  INT_SRC_MAC_TXRX_MISC,            /* 32 */
  INT_SRC_MAC_RX_TRIGGER,           /* 33 */
  INT_SRC_MAC_TX_TRIGGER,           /* 34 */
  INT_SRC_MAC_PROT_TRIGGER,         /* 35 */
  INT_SRC_MAC_GENERAL,              /* 36 */
  INT_SRC_MAC_HSU,                  /* 37 */
  INT_SRC_GPIO_NS = INT_SRC_MAC_HSU,
  INT_SRC_MAC_WAKEUP,               /* 38 */
  INT_SRC_BTDM,
  INT_SRC_BLE,
  INT_SRC_BT,
  INT_SRC_QSPI1,
  INT_SRC_PWM1,
  INT_SRC_I2S1,
  INT_SRC_I2S2,
  INT_SRC_H264,
  INT_SRC_SDMADC,
  INT_SRC_ETH,
  INT_SRC_SCALE0,
  INT_SRC_BMC64,
  INT_SRC_PLL_UNLOCK,
  INT_SRC_TOUCHED,
  INT_SRC_USB_PLUG_INOUT,
  INT_SRC_RTC,
  INT_SRC_GPIO,
  INT_SRC_DMA1_SEC,
  INT_SRC_DMA1_NSEC,
  INT_SRC_YUVB,
  INT_SRC_ROTT,
  INT_SRC_7816,
  INT_SRC_LIN,
  INT_SRC_SCALE1,
  INT_SRC_MAILBOX,
  INT_SRC_NONE
} icu_int_src_t;

typedef void (*int_group_isr_t)(void *arg);
typedef void (*int_mac_ps_callback_t)(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_INT_TYPES_H */

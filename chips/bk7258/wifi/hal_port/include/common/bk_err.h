/*
 * chips/bk7258/wifi/hal_port/include/common/bk_err.h
 *
 * NuttX reimplementation of the Armino error-code macros used by the vendored
 * glue. Values are interface-compatible (the glue branches on them). The
 * BK_LOG_ON_ERR/BK_RETURN_ON_ERR/BK_ABORT_ON_ERR/BK_RETURN_ON_NULL helpers are
 * mapped onto NuttX logging; bk_reboot is a documented no-op at skeleton stage.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_ERR_H
#define __BK7258_WIFI_GLUE_COMMON_BK_ERR_H

#include <stdint.h>
#include <stddef.h>

#include <components/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int bk_err_t;

#define ERR_TAG "err"

#define BK_OK                      0
#define BK_FAIL                    -1

#define BK_ERR_COMMON_BASE         (-0x1000)
#define BK_ERR_WIFI_BASE           (-0x1200)
#define BK_ERR_WPA_BASE            (-0x1400)
#define BK_ERR_BLE_BASE            (-0x1600)
#define BK_ERR_RWNX_BASE           (-0x1800)
#define BK_ERR_HITF_BASE           (-0x1900)
#define BK_ERR_EVENT_BASE          (-0x1b00)
#define BK_ERR_NETIF_BASE          (-0x1c00)
#define BK_ERR_PWM_BASE            (-0x1d00)
#define BK_ERR_PWM_HAL_BASE        (-0x1e00)
#define BK_ERR_TIMER_BASE          (-0x1f00)
#define BK_ERR_GPIO_BASE           (-0x2000)
#define BK_ERR_DMA_BASE            (-0x2100)
#define BK_ERR_DMA_HAL_BASE        (-0x2200)
#define BK_ERR_RF_BASE             (-0x2300)
#define BK_ERR_UART_BASE           (-0x2400)
#define BK_ERR_INT_BASE            (-0x2500)
#define BK_ERR_WDT_BASE            (-0x2600)
#define BK_ERR_TRNG_BASE           (-0x2700)
#define BK_ERR_EFUSE_BASE          (-0x2800)
#define BK_ERR_MAC_BASE            (-0x2820)
#define BK_ERR_TEMPD_BASE          (-0x2840)
#define BK_ERR_ADC_BASE            (-0x2900)
#define BK_ERR_SPI_BASE            (-0x2a00)
#define BK_ERR_MAILBOX_BASE        (-0x2b00)
#define BK_ERR_QSPI_BASE           (-0x2c00)
#define BK_ERR_I2C_BASE            (-0x2d00)
#define BK_ERR_VAULT_BASE          (-0x2e00)
#define BK_ERR_AON_RTC_BASE        (-0x2f00)
#define BK_ERR_JPEG_BASE           (-0x3000)
#define BK_ERR_AUD_BASE            (-0x3100)
#define BK_ERR_FFT_BASE            (-0x3200)
#define BK_ERR_TOUCH_BASE          (-0x3300)
#define BK_ERR_I2S_BASE            (-0x3400)
#define BK_ERR_CALENDAR_BASE       (-0x3500)
#define BK_ERR_SBC_BASE            (-0x3600)
#define BK_ERR_FLASH_BASE          (-0x3700)
#define BK_ERR_SDIO_BASE           (-0x3800)
#define BK_ERR_SDIO_HOST_BASE      (-0x3900)
#define BK_ERR_MPC_BASE            (-0x3a00)
#define BK_ERR_PSRAM_BASE          (-0x3b00)
#define BK_ERR_PRRO_BASE           (-0x3c00)
#define BK_ERR_AON_WDT_BASE        (-0x3d00)
#define BK_ERR_JPEGDEC_BASE        (-0x3e00)
#define BK_ERR_LCD_BASE            (-0x3f00)
#define BK_ERR_DMA2D_BASE          (-0x4000)
#define BK_ERR_YUV_BUF_BASE        (-0x4100)
#define BK_ERR_UVC_BASE            (-0x4200)
#define BK_ERR_H264_BASE           (-0x4300)
#define BK_ERR_USB_BASE            (-0x4400)
#define BK_ERR_OTP_BASE            (-0x4500)
#define BK_ERR_CKMN_BASE           (-0x4600)
#define BK_ERR_LIN_BASE            (-0x4700)
#define BK_ERR_SCR_BASE            (-0x4800)
#define BK_ERR_CAN_BASE            (-0x4900)
#define BK_ERR_OTA_BASE            (-0x4a00)
#define BK_ERR_RTOS_BASE           (-0x1a2c)

#define BK_ERR_NOT_INIT            (BK_ERR_COMMON_BASE)
#define BK_ERR_PARAM               (BK_ERR_COMMON_BASE - 1)
#define BK_ERR_NOT_FOUND           (BK_ERR_COMMON_BASE - 2)
#define BK_ERR_OPEN                (BK_ERR_COMMON_BASE - 3)
#define BK_ERR_IN_PROGRESS         (BK_ERR_COMMON_BASE - 4)
#define BK_ERR_NO_MEM              (BK_ERR_COMMON_BASE - 5)
#define BK_ERR_TIMEOUT             (BK_ERR_COMMON_BASE - 6)
#define BK_ERR_STATE               (BK_ERR_COMMON_BASE - 7)
#define BK_ERR_TRY_AGAIN           (BK_ERR_COMMON_BASE - 8)
#define BK_ERR_NULL_PARAM          (BK_ERR_COMMON_BASE - 9)
#define BK_ERR_NOT_SUPPORT         (BK_ERR_COMMON_BASE - 10)
#define BK_ERR_BUSY                (BK_ERR_COMMON_BASE - 11)
#define BK_ERR_PATH                (BK_ERR_COMMON_BASE - 12)
#define BK_ERR_IS_FALL             (BK_ERR_COMMON_BASE - 13)
#define BK_ERR_IS_EXIST            (BK_ERR_COMMON_BASE - 14)
#define BK_ERR_NO_DEV              (BK_ERR_COMMON_BASE - 15)
#define BK_ERR_SHUT_DOWN           (BK_ERR_COMMON_BASE - 16)

void bk_reboot(void);

#define BK_LOG_ON_ERR(_x) do { \
  bk_err_t _err = (_x); \
  if (_err != BK_OK) { \
    BK_LOGE(ERR_TAG, "%s %d: ret=-0x%x\n", __FUNCTION__, __LINE__, -_err); \
  } \
} while (0)

#define BK_RETURN_ON_ERR(_x) do { \
  bk_err_t _err = (_x); \
  if (_err != BK_OK) { \
    return _err; \
  } \
} while (0)

#define BK_ABORT_ON_ERR(_x) do { \
  bk_err_t _err = (_x); \
  if (_err != BK_OK) { \
    BK_LOGE(ERR_TAG, "%s %d: ret=-0x%x\n", __FUNCTION__, __LINE__, -_err); \
    bk_reboot(); \
  } \
} while (0)

#define BK_RETURN_ON_NULL(_x) do { \
  if (!(_x)) { \
    BK_LOGE(ERR_TAG, "Null %s\n", __FUNCTION__); \
    return BK_ERR_NULL_PARAM; \
  } \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_ERR_H */

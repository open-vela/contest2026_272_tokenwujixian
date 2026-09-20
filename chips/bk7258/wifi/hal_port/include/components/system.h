/*
 * chips/bk7258/wifi/hal_port/include/components/system.h
 *
 * NuttX reimplementation of the Armino system API used by the vendored glue.
 * Enums/macros are interface-compatible; printf maps onto NuttX stdio,
 * reboot onto up_systemreset, and the tick macros onto NuttX clock constants.
 */

#ifndef __BK7258_WIFI_GLUE_COMPONENTS_SYSTEM_H
#define __BK7258_WIFI_GLUE_COMPONENTS_SYSTEM_H

#include <stdarg.h>
#include <stdint.h>

#include <nuttx/clock.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RESET_SOURCE_POWERON = 0x0,
  RESET_SOURCE_REBOOT = 0x1,
  RESET_SOURCE_WATCHDOG = 0x2,
  RESET_SOURCE_DEEPPS_GPIO = 0x3,
  RESET_SOURCE_DEEPPS_RTC = 0x4,
  RESET_SOURCE_DEEPPS_USB = 0x5,
  RESET_SOURCE_DEEPPS_TOUCH = 0x6,
  RESET_SOURCE_CRASH_ILLEGAL_JUMP = 0x7,
  RESET_SOURCE_CRASH_UNDEFINED = 0x8,
  RESET_SOURCE_CRASH_PREFETCH_ABORT = 0x9,
  RESET_SOURCE_CRASH_DATA_ABORT = 0xa,
  RESET_SOURCE_CRASH_UNUSED = 0xb,
  RESET_SOURCE_CRASH_ILLEGAL_INSTRUCTION = 0xc,
  RESET_SOURCE_CRASH_MISALIGNED = 0xd,
  RESET_SOURCE_CRASH_ASSERT = 0xe,
  RESET_SOURCE_SUPER_DEEP = 0xf,
  RESET_SOURCE_NMI_WDT = 0x10,
  RESET_SOURCE_HARD_FAULT = 0x11,
  RESET_SOURCE_MPU_FAULT = 0x12,
  RESET_SOURCE_BUS_FAULT = 0x13,
  RESET_SOURCE_USAGE_FAULT = 0x14,
  RESET_SOURCE_SECURE_FAULT = 0x15,
  RESET_SOURCE_DEBUG_MONITOR_FAULT = 0x16,
  RESET_SOURCE_DEFAULT_EXCEPTION = 0x17,
  RESET_SOURCE_OTA_REBOOT = 0x18,
  RESET_SOURCE_FORCE_DEEPSLEEP = 0x19,
  RESET_SOURCE_UNKNOWN = 0xff,
} RESET_SOURCE_STATUS;

typedef enum {
  MAC_TYPE_BASE = 0,
  MAC_TYPE_STA,
  MAC_TYPE_AP,
  MAC_TYPE_BLUETOOTH,
  MAC_TYPE_ETH,
  MAC_TYPE_P2P,
  MAC_MAX,
} mac_type_t;

#define BK_ERR_INVALID_MAC_TYPE   (BK_ERR_MAC_BASE)
#define BK_ERR_ZERO_MAC           (BK_ERR_MAC_BASE - 1)
#define BK_ERR_GROUP_MAC          (BK_ERR_MAC_BASE - 2)
#define BK_ERR_INVALID_MAC        (BK_ERR_MAC_BASE - 3)
#define BK_MAC_ADDR_LEN           6
#define BK_IS_ZERO_MAC(m)  (((m)[0] == 0) && ((m)[1] == 0) && ((m)[2] == 0) && \
                            ((m)[3] == 0) && ((m)[4] == 0) && ((m)[5] == 0))
#define BK_IS_GROUP_MAC(m) ((m)[0] & 0x01)

#define rtos_get_ms_per_tick()   (MSEC_PER_TICK)
#define TICK_PER_SECOND          (TICK_PER_SEC)
#define rtos_get_tick_per_second() (TICK_PER_SECOND)
#define BK_MS_TO_TICKS(x)        MSEC2TICK(x)
#define BK_TICKS_TO_MS(x)        TICK2MSEC(x)

bk_err_t bk_set_base_mac(const uint8_t *mac);
bk_err_t bk_get_mac(uint8_t *mac, mac_type_t type);
void     bk_reboot(void);
void     bk_reboot_ex(uint32_t reset_reason);

uint64_t bk_get_tick(void);
uint32_t bk_get_second(void);
uint32_t bk_get_ms_per_tick(void);
uint32_t bk_get_ticks_per_second(void);

void     bk_printf(const char *fmt, ...);
void     bk_null_printf(const char *fmt, ...);
int      bk_printf_init(void);
int      bk_printf_deinit(void);
void     bk_set_printf_enable(uint8_t enable);
void     bk_set_printf_sync(uint8_t enable);
int      bk_get_printf_sync(void);
void     bk_printf_ex(int level, char *tag, const char *fmt, ...);
void     bk_printf_ext(int level, char *tag, const char *fmt, ...);
void     bk_printf_nonblock(int level, char *tag, const char *fmt, ...);
void     bk_printf_static_nonblock(int level, char *tag, const char *fmt, ...);
void     bk_printf_static_block(int level, char *tag, const char *fmt, ...);
void     bk_vprintf_ext(int level, char *tag, const char *fmt, va_list args);
void     bk_printf_raw(int level, char *tag, const char *fmt, ...);
void     bk_printf_raw_nonblock(int level, char *tag, const char *fmt, ...);
void     bk_vprintf_raw(int level, char *tag, const char *fmt, va_list args);
void     bk_disable_mod_printf(char *mod_name, uint8_t disable);
char    *bk_get_disable_mod(int *idx);
void     bk_set_printf_port(uint8_t port_num);
int      bk_get_printf_port(void);

uint32_t bk_misc_get_reset_reason(void);
void     bk_misc_set_reset_reason(uint32_t type);
uint32_t bk_misc_get_cp_reset_reason(void);
uint32_t bk_misc_get_ap_reset_reason(void);
void     bk_misc_set_ap_reset_reason(uint32_t type);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMPONENTS_SYSTEM_H */

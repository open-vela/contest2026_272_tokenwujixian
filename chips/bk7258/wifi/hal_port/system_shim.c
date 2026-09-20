/*
 * chips/bk7258/wifi/hal_port/system_shim.c
 *
 * NuttX implementation of the Armino system API (printf/reboot/mac/tick)
 * used by the vendored glue. printf maps onto syslog, which is safe to call
 * from the vendor's IRQ handlers; reboot onto up_systemreset. MAC reading is
 * deferred to the chip layer (efuse/OTP).
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <syslog.h>

#include "components/system.h"
#include "driver/uart.h"

/****************************************************************************
 * MAC
 ****************************************************************************/

/* bk_get_mac / bk_set_base_mac MOVED to hal_port/hal_port_mac.c (2026-09-01),
 * ported from cp/components/bk_system/mac.c.
 *
 * The implementation that used to live here accepted only MAC_TYPE_BASE and
 * returned BK_FAIL for every other type WITHOUT writing the caller's buffer.
 * The closed library requests MAC_TYPE_STA via bk_wifi_sta_get_mac()
 * (wifi_v2.c:4481), which discards the return value and always reports BK_OK,
 * so callers -- including scan_probe_req_tx and the MM_ADD_IF_REQ payload --
 * silently received uninitialised memory.  Consistent with NXMAC's address
 * registers reading zero on our board (0x49100010/0x14) where the authority
 * holds C8:47:8C:46:02:15. */

/****************************************************************************
 * Reboot
 ****************************************************************************/

void bk_reboot(void)
{
  up_systemreset();
}

void bk_reboot_ex(uint32_t reset_reason)
{
  (void)reset_reason;
  up_systemreset();
}

/****************************************************************************
 * Tick / time
 ****************************************************************************/

uint64_t bk_get_tick(void)
{
  return (uint64_t)clock_systime_ticks();
}

uint32_t bk_get_second(void)
{
  return (uint32_t)(clock_systime_ticks() / TICK_PER_SECOND);
}

uint32_t bk_get_ms_per_tick(void)
{
  return (uint32_t)MSEC_PER_TICK;
}

uint32_t bk_get_ticks_per_second(void)
{
  return (uint32_t)TICK_PER_SECOND;
}

/****************************************************************************
 * printf family
 ****************************************************************************/

int bk_printf_init(void)
{
  return 0;
}

int bk_printf_deinit(void)
{
  return 0;
}

/* syslog rather than printf, because the vendor logs from hard IRQ context.
 *
 * printf() takes the stdout FILE lock through flockfile() -> nxmutex_wait(),
 * whose first DEBUGASSERT is !up_interrupt_context() (semaphore.h:518).  The
 * vendor registers bk_printf_ext as the ._log op of both the Wi-Fi and the PHY
 * adapter (bk_wifi_adapter.c:1389, bk_phy_adapter.c:603), and the MAC hardware
 * IRQ handler calls it, so one log line on that path takes the system down:
 *
 *   exception_direct -> bk7258_wifi_isr_trampoline -> hal_machw_gen_handler
 *     -> bk_printf_ext -> vfprintf -> flockfile -> nxmutex_wait -> _assert
 *
 * Board evidence: 0914-vela-17 asserted at semaphore.h:518 with exactly that
 * frame chain, once in eighteen runs, which fits a rare error path inside the
 * vendor IRQ handler rather than a race.
 *
 * syslog() is the NuttX facility for this: syslog_write.c:65 downgrades to a
 * non-blocking write in interrupt context, and syslog_device.c:261 returns
 * -ENOSYS instead of asserting when the device is not open yet.  The crash dump
 * that exposed the bug was itself written from interrupt context through
 * syslog.  The same rule is already stated for queues in
 * bk7258_wifi_osal_queue_send_common(); this file had missed it.
 *
 * The tagged variants format into one buffer and emit a single syslog call,
 * because nx_vsyslog() prepends a timestamp per call
 * (vsyslog.c:106-163, CONFIG_SYSLOG_TIMESTAMP=y): a separate call for the tag
 * would print two timestamps and split the line.
 *
 * The level argument stays ignored, as it was with printf: the vendor's levels
 * do not map onto syslog priorities and nothing here depends on the
 * distinction.
 *
 * ponytail: 192-byte line cap; the longest vendor lines seen on the board are
 * ~130 bytes (lmac_connect_req), and this runs on the 2 KiB IRQ stack.  Raise
 * it only if a real line is seen truncated. */

#define BK7258_LOG_LINE_MAX 192

static void bk7258_vlog_tagged(FAR const char *tag, FAR const char *fmt,
                               va_list ap)
{
  char line[BK7258_LOG_LINE_MAX];

  vsnprintf(line, sizeof(line), fmt, ap);
  syslog(LOG_INFO, "[%s] %s", tag, line);
}

void bk_printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_null_printf(const char *fmt, ...)
{
  (void)fmt;
}

void bk_printf_ex(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  bk7258_vlog_tagged(tag, fmt, ap);
  va_end(ap);
  (void)level;
}

void bk_printf_ext(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  bk7258_vlog_tagged(tag, fmt, ap);
  va_end(ap);
  (void)level;
}

void bk_printf_raw(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  (void)tag;
  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
  (void)level;
}

void bk_vprintf_ext(int level, char *tag, const char *fmt, va_list args)
{
  bk7258_vlog_tagged(tag, fmt, args);
  (void)level;
}

void bk_vprintf_raw(int level, char *tag, const char *fmt, va_list args)
{
  (void)tag;
  vsyslog(LOG_INFO, fmt, args);
  (void)level;
}

void bk_set_printf_enable(uint8_t enable)
{
  (void)enable;
}

void bk_set_printf_sync(uint8_t enable)
{
  (void)enable;
}

int bk_get_printf_sync(void)
{
  /* Matches the authority fallback when CONFIG_SHELL_ASYNCLOG is disabled. */
  return 1;
}

void bk_set_printf_port(uint8_t port_num)
{
  (void)port_num;
}

int bk_get_printf_port(void)
{
  return 0;
}

bk_err_t uart_write_string(uart_id_t id, const char *string)
{
  (void)id;
  if (string != NULL)
    {
      fputs(string, stdout);
    }
  return BK_OK;
}

/****************************************************************************
 * Reset reason
 ****************************************************************************/

static uint32_t g_bk_reset_reason = RESET_SOURCE_UNKNOWN;

uint32_t bk_misc_get_reset_reason(void)
{
  return g_bk_reset_reason;
}

void bk_misc_set_reset_reason(uint32_t type)
{
  g_bk_reset_reason = type;
}

uint32_t bk_misc_get_cp_reset_reason(void)
{
  return g_bk_reset_reason;
}

uint32_t bk_misc_get_ap_reset_reason(void)
{
  return g_bk_reset_reason;
}

void bk_misc_set_ap_reset_reason(uint32_t type)
{
  (void)type;
}

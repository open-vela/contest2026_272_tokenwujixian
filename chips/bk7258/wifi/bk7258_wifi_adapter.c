/*
 * BK7258 NuttX owner for the Armino Wi-Fi OS capability ABI.
 *
 * The generated wifi_os_funcs_t/wifi_os_variable_t declarations are retained
 * verbatim. This file only assembles already-owned NuttX providers; the
 * original Armino bk_wifi_adapter.c remains the field/semantic reference and
 * is intentionally not compiled because it is lwIP/FreeRTOS-bound.
 */

#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>

#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <common/bk_include.h>
#include <bk_ps.h>
#include <bk_sys_ctrl.h>
#include <driver/int.h>
#include <driver/aon_rtc.h>
#include <generated/lmac_wifi_adapter.h>
#include <gpio_driver.h>
#include <modules/pm.h>
#include <sys_types.h>
#include "bk_phy_internal.h"
#include "bk_rf_internal.h"
#include "bk_phy_adapter.h"
#include "bk_private/bk_wifi.h"

#include <arch/chip/bk7258_sysctrl.h>
#include <arch/chip/bk7258_clock.h>

#include "bk7258_wifi_internal.h"
#include "bk_feature.h"
#include "os/mem.h"
#include "os/os.h"
#include "os/str.h"
#include "stack_base.h"
#include "sys_driver.h"
#include "sys_ll.h"

extern bk_err_t dma_memcpy(void *out, const void *in, uint32_t len);
extern void bk_printf(const char *fmt, ...);
extern void bk_printf_ext(int level, char *tag, const char *fmt, ...);
extern void bk_null_printf(const char *fmt, ...);
extern void bk_printf_raw(int level, char *tag, const char *fmt, ...);
extern bk_err_t bk_wifi_interrupt_init(void);
extern bool ate_is_enabled(void);
extern void *bk_pbuf_alloc_wrapper(int layer, uint16_t length, int type);
extern void bk_pbuf_free_wrapper(void *p);
extern void bk_pbuf_ref_wrapper(void *p);
extern void bk_pbuf_header_wrapper(void *p, int16_t len);
extern void bk_pbuf_cat_wrapper(void *p, void *q);
extern void *bk_pbuf_coalesce_wrapper(void *p);
extern int bk_get_rx_pbuf_type_wrapper(void);
extern int bk_get_pbuf_pool_size_wrapper(void);

extern bk_err_t rtos_lock_mutex(beken_mutex_t *mutex);
extern bk_err_t rtos_unlock_mutex(beken_mutex_t *mutex);
extern void bk7258_wifi_adapter_register_callbacks(void);
/* Declared by bk_phy_adapter.h.  Keep these explicit because this adapter
 * otherwise only needs the smaller PHY-internal API surface. */
extern void rwnx_cal_mac_sleep_rc_clr(void);
extern void rwnx_cal_mac_sleep_rc_recover(void);
extern int rw_msg_send(const void *msg_params, int reqcfm, uint16_t reqid,
                       void *cfm);
extern UINT8 rw_ieee80211_init_scan_chan(void *req);
extern UINT8 rw_ieee80211_get_scan_default_chan_num(void);
extern void rwnx_set_wifi_rlk_start(uint32_t start);
extern uint32_t sr_get_scan_number(void);
extern void *sr_get_scan_results(void);
extern void sr_flush_scan_results(void *rst_ptr);
extern void *rwnx_get_rlk_info_results(void);
extern void rwnx_flush_rlk_info_results(void);
extern void net_begin_send_arp_reply(int is_send_arp, int is_allow_send_req);

static char *bk7258_wifi_strdup_cb(const char *s) { return strdup(s); }
static unsigned int bk7258_wifi_strlcpy_cb(char *d, const char *s, size_t n)
{
  return strlcpy(d, s, n);
}
static INT32 bk7258_wifi_snprintf_cb(char *d, UINT32 n, const char *f, ...)
{
  va_list ap;
  int ret;
  va_start(ap, f);
  ret = vsnprintf(d, n, f, ap);
  va_end(ap);
  return (INT32)ret;
}
static UINT32 bk7258_wifi_strtoul_cb(const char *s, char **e, int b)
{
  return (UINT32)strtoul(s, e, b);
}
static INT32 bk7258_wifi_strcasecmp_cb(const char *a, const char *b)
{
  return strcasecmp(a, b);
}
static INT32 bk7258_wifi_strncasecmp_cb(const char *a, const char *b, UINT32 n)
{
  return strncasecmp(a, b, n);
}
static bk_err_t bk7258_wifi_lock_mutex_cb(void **mutex)
{
  return rtos_lock_mutex((beken_mutex_t *)mutex);
}
static bk_err_t bk7258_wifi_unlock_mutex_cb(void **mutex)
{
  return rtos_unlock_mutex((beken_mutex_t *)mutex);
}

/* The vendor runtime declares these as externally visible pointers. They are
 * statically bound to the NuttX-owned tables, but this object never calls
 * bk_wifi_init(); runtime startup remains explicitly disabled. */
extern wifi_os_funcs_t g_wifi_os_funcs;
extern wifi_os_variable_t g_wifi_os_variable;
wifi_os_funcs_t *g_wifi_funcs = &g_wifi_os_funcs;
wifi_os_variable_t *g_wifi_vars = &g_wifi_os_variable;

static void *bk7258_wifi_os_malloc_cb(const char *func, int line, size_t size)
{
  (void)func;
  (void)line;
  return bk7258_wifi_osal_malloc(size);
}

static void *bk7258_wifi_os_zalloc_cb(const char *func, int line, size_t size)
{
  (void)func;
  (void)line;
  return bk7258_wifi_osal_zalloc(size);
}

static void bk7258_wifi_os_free_cb(void *ptr)
{
  bk7258_wifi_osal_free(ptr);
}

static void *bk7258_wifi_os_realloc_cb(void *ptr, size_t size)
{
  return bk7258_wifi_osal_realloc(ptr, size);
}

static INT32 bk7258_wifi_memcmp_cb(const void *a, const void *b, UINT32 n)
{
  return (INT32)memcmp(a, b, n);
}

static void *bk7258_wifi_memset_cb(void *dst, int value, UINT32 n)
{
  return memset(dst, value, n);
}

static void *bk7258_wifi_memcpy_cb(void *dst, const void *src, UINT32 n)
{
  return memcpy(dst, src, n);
}

static void *bk7258_wifi_memmove_cb(void *dst, const void *src, UINT32 n)
{
  return memmove(dst, src, n);
}

static UINT32 bk7258_wifi_strlen_cb(const char *str)
{
  return (UINT32)strlen(str);
}

static INT32 bk7258_wifi_strcmp_cb(const char *a, const char *b)
{
  return (INT32)strcmp(a, b);
}

static INT32 bk7258_wifi_strncmp_cb(const char *a, const char *b, UINT32 n)
{
  return (INT32)strncmp(a, b, n);
}

static char *bk7258_wifi_strcpy_cb(char *dst, const char *src)
{
  return strcpy(dst, src);
}

static char *bk7258_wifi_strncpy_cb(char *dst, const char *src, UINT32 n)
{
  return strncpy(dst, src, n);
}

static char *bk7258_wifi_strchr_cb(const char *str, int ch)
{
  return (char *)strchr(str, ch);
}

static char *bk7258_wifi_strrchr_cb(const char *str, int ch)
{
  return (char *)strrchr(str, ch);
}

static char *bk7258_wifi_strstr_cb(const char *haystack, const char *needle)
{
  return (char *)strstr(haystack, needle);
}

static void bk7258_wifi_delay_cb(INT32 ticks)
{
  if (ticks > 0)
    {
      bk7258_wifi_osal_delay_ms((uint32_t)ticks);
    }
}

static void bk7258_wifi_delay_us_cb(UINT32 usec)
{
  bk7258_wifi_osal_delay_us(usec);
}

static int bk7258_wifi_delay_ms_cb(uint32_t msec)
{
  bk7258_wifi_osal_delay_ms(msec);
  return 0;
}

static uint32_t bk7258_wifi_time_cb(void)
{
  return (uint32_t)bk7258_wifi_osal_time_ms();
}

static uint32_t bk7258_wifi_enter_critical_cb(void)
{
  return bk7258_wifi_osal_enter_critical();
}

static void bk7258_wifi_exit_critical_cb(uint32_t flags)
{
  bk7258_wifi_osal_exit_critical(flags);
}

static unsigned long bk7258_wifi_ms_to_ticks_cb(unsigned long ms)
{
  return (unsigned long)MSEC2TICK(ms);
}

static uint32_t bk7258_wifi_ms_per_tick_cb(void)
{
  return MSEC_PER_TICK;
}

static bk_err_t bk7258_wifi_dma_memcpy_cb(void *out, const void *in,
                                          uint32_t len)
{
  return dma_memcpy(out, in, len);
}

static bk_err_t bk7258_wifi_isr_register_cb(uint32_t src, void *isr, void *arg)
{
  return bk_int_isr_register((icu_int_src_t)src,
                             (int_group_isr_t)isr, arg);
}

static int32 bk7258_wifi_sys_drv_int_enable_cb(uint32 param)
{
  return (int32)sys_drv_int_enable(param);
}

static int32 bk7258_wifi_sys_drv_int_disable_cb(uint32 param)
{
  return (int32)sys_drv_int_disable(param);
}

static int32 bk7258_wifi_sys_drv_int_group2_enable_cb(uint32 param)
{
  return (int32)sys_drv_int_group2_enable(param);
}

static int32 bk7258_wifi_sys_drv_int_group2_disable_cb(uint32 param)
{
  return (int32)sys_drv_int_group2_disable(param);
}

static int32 bk7258_wifi_sys_drv_module_power_state_get_cb(uint32_t module)
{
  return (int32)sys_drv_module_power_state_get((power_module_name_t)module);
}

static bk_err_t bk7258_wifi_pm_vote_sleep_cb(uint32_t module,
                                              uint32_t sleep_state,
                                              uint32_t sleep_time)
{
  return bk_pm_module_vote_sleep_ctrl((pm_sleep_module_name_e)module,
                                      (uint32)sleep_state, (uint32)sleep_time);
}

static uint32_t bk7258_wifi_pm_lpo_src_cb(void)
{
  uint32_t v = (uint32_t)bk_pm_lpo_src_get();
  static uint32_t last;
  static bool first = true;

  if (first || v != last)
    {
      first = false;
      last = v;
      syslog(LOG_INFO, "[BK7258-WIFI] pmq: lpo_src=%u\n", v);
    }

  return v;
}

static int32 bk7258_wifi_pm_module_power_state_cb(unsigned int module)
{
  return bk_pm_module_power_state_get((pm_power_module_name_e)module);
}

static bk_err_t bk7258_wifi_pm_vote_power_cb(unsigned int module,
                                              uint32_t power_state)
{
  return bk_pm_module_vote_power_ctrl((pm_power_module_name_e)module,
                                      (pm_power_module_state_e)power_state);
}

static bk_err_t bk7258_wifi_pm_vote_cpu_freq_cb(uint32_t module,
                                                 uint32_t cpu_freq)
{
  /* Behavioral logger (bring-up diagnostic): crm_clk_set feeds the mode
   * code it computed through this slot; the log line reveals the live
   * rwnxl_compute_cpu_freq output that selects the clk_config row. */
  bk_err_t ret = bk_pm_module_vote_cpu_freq((pm_dev_id_e)module,
                                            (pm_cpu_freq_e)cpu_freq);
  syslog(LOG_INFO,
         "[BK7258-WIFI] pmq: vote_cpu_freq(module=%u freq=%u) ret=%d\n",
         module, cpu_freq, (int)ret);
  return ret;
}

static uint64_t bk7258_wifi_aon_rtc_tick_cb(uint32_t id)
{
  return bk_aon_rtc_get_current_tick((aon_rtc_id_t)id);
}

static float bk7258_wifi_rtc_ms_tick_cb(void)
{
  return (float)bk_rtc_get_ms_tick_count();
}

static pm_cb_notify g_bk7258_wifi_32k_ready_cb;

static void bk7258_wifi_mac_ps_exc32_init_cb(void *callback)
{
  pm_cb_extern32k_cfg_t config = {0};

  config.cb_module = PM_32K_MODULE_WIFI;
  config.cb_func = (pm_cb_extern32k)callback;
  if (pm_extern32k_register_cb(&config) != BK_OK)
    {
      bk_printf("[BK7258-WIFI] extern32k callback registration failed\n");
    }
}

static void bk7258_wifi_32k_ready_register_cb(void *callback)
{
  g_bk7258_wifi_32k_ready_cb = (pm_cb_notify)callback;
}

static void bk7258_wifi_mac_ps_exc32_notify_cb(void)
{
  if (g_bk7258_wifi_32k_ready_cb != NULL)
    {
      g_bk7258_wifi_32k_ready_cb();
    }
}

static bk_err_t bk7258_wifi_queue_send_cb(void **queue, void *msg, uint32_t t)
{ return rtos_push_to_queue((beken_queue_t *)queue, msg, t); }
static bool bk7258_wifi_queue_full_cb(void **queue)
{ return rtos_is_queue_full((beken_queue_t *)queue); }
static bool bk7258_wifi_queue_empty_cb(void **queue)
{ return rtos_is_queue_empty((beken_queue_t *)queue); }
static bk_err_t bk7258_wifi_queue_recv_cb(void **queue, void *msg, uint32_t t)
{ return rtos_pop_from_queue((beken_queue_t *)queue, msg, t); }
static bk_err_t bk7258_wifi_queue_front_cb(void **queue, void *msg, uint32_t t)
{ return rtos_push_to_queue_front((beken_queue_t *)queue, msg, t); }
static bk_err_t bk7258_wifi_sem_init_cb(void **sem, int max)
{ return rtos_init_semaphore((beken_semaphore_t *)sem, max); }
static bk_err_t bk7258_wifi_sem_wait_cb(void **sem, uint32_t t)
{ return rtos_get_semaphore((beken_semaphore_t *)sem, t); }
static bk_err_t bk7258_wifi_sem_deinit_cb(void **sem)
{ return rtos_deinit_semaphore((beken_semaphore_t *)sem); }
static int bk7258_wifi_sem_post_cb(void **sem)
{ return rtos_set_semaphore((beken_semaphore_t *)sem); }
static bk_err_t bk7258_wifi_thread_create_cb(void **thread, uint8_t p,
                                             const char *name, void *fn,
                                             uint32_t stack, void *arg)
{ return rtos_create_thread((beken_thread_t *)thread, p, name, fn, stack, arg); }
static bk_err_t bk7258_wifi_thread_delete_cb(void **thread)
{ return rtos_delete_thread((beken_thread_t *)thread); }
static bool bk7258_wifi_is_current_thread_cb(void **thread)
{ return rtos_is_current_thread((beken_thread_t *)thread); }
static bk_err_t bk7258_wifi_queue_init_cb(void **q, const char *name,
                                          uint32_t size, uint32_t count)
{ return rtos_init_queue((beken_queue_t *)q, name, size, count); }
static bk_err_t bk7258_wifi_queue_deinit_cb(void **q)
{ return rtos_deinit_queue((beken_queue_t *)q); }
static bk_err_t bk7258_wifi_mutex_init_cb(void **m)
{ return rtos_init_mutex((beken_mutex_t *)m); }
static bk_err_t bk7258_wifi_mutex_deinit_cb(void **m)
{ return rtos_deinit_mutex((beken_mutex_t *)m); }
static int bk7258_wifi_timer_init_cb(void *timer, uint32_t ms, void *fn, void *arg)
{ return rtos_init_timer((beken_timer_t *)timer, ms, (timer_handler_t)fn, arg); }
static bk_err_t bk7258_wifi_timer_reload_cb(void *timer)
{ return rtos_reload_timer((beken_timer_t *)timer); }
static bool bk7258_wifi_timer_running_cb(void *timer)
{ return rtos_is_timer_running((beken_timer_t *)timer); }
static int bk7258_wifi_timer_stop_cb(void *timer)
{ return rtos_stop_timer((beken_timer_t *)timer); }

static void bk7258_wifi_register_dump_hook_cb(void *wifi_func)
{
  rtos_regist_wifi_dump_hook((hook_func)wifi_func);
}

/* CSV batch-closure providers.  Each mirrors a source-backed Armino adapter
 * operation or an explicit NuttX-owned compatibility contract; none reports a
 * hardware or network capability that this port does not own. */
static void bk7258_wifi_mac_printf_encode_cb(char *txt, size_t maxlen,
                                              const u8 *data, size_t len)
{
  char *end;
  size_t index;

  if (txt == NULL || maxlen == 0)
    {
      return;
    }

  end = txt + maxlen;
  for (index = 0; index < len; index++)
    {
      if (txt + 4 >= end)
        {
          break;
        }

      switch (data[index])
        {
          case '\"':
            *txt++ = '\\';
            *txt++ = '\"';
            break;
          case '\\':
            *txt++ = '\\';
            *txt++ = '\\';
            break;
          case '\033':
            *txt++ = '\\';
            *txt++ = 'e';
            break;
          case '\n':
            *txt++ = '\\';
            *txt++ = 'n';
            break;
          case '\r':
            *txt++ = '\\';
            *txt++ = 'r';
            break;
          case '\t':
            *txt++ = '\\';
            *txt++ = 't';
            break;
          default:
            if (data[index] >= 32 && data[index] <= 126)
              {
                *txt++ = (char)data[index];
              }
            else
              {
                txt += snprintf(txt, (size_t)(end - txt), "\\x%02x",
                                data[index]);
              }
            break;
        }
    }

  *txt = '\0';
}

static void bk7258_wifi_set_sta_status_cb(void *info)
{
  if (info == NULL)
    {
      bk_printf("[BK7258-WIFI] _set_sta_status received NULL info\n");
      PANIC();
    }

  mhdr_set_station_status(*(wifi_linkstate_reason_t *)info);
}

static void bk7258_wifi_rtos_assert_cb(uint32_t expression)
{
  if (expression == 0)
    {
      bk_printf("[BK7258-WIFI] vendor rtos assertion failed\n");
      PANIC();
    }
}

static int bk7258_wifi_shell_assert_out_cb(bool continue_execution,
                                            char *format, ...)
{
  char message[160];
  va_list ap;

  va_start(ap, format);
  (void)vsnprintf(message, sizeof(message), format, ap);
  va_end(ap);
  bk_printf("[BK7258-WIFI] vendor assertion: %s", message);

  if (continue_execution)
    {
      return 1;
    }

  PANIC();
}

static void *bk7258_wifi_get_netif_hostname_cb(void *netif)
{
  static char hostname[] = "bk7258";

  (void)netif;
  return hostname;
}

static uint32_t bk7258_wifi_lwip_ntohl_cb(uint32_t value)
{
  return ntohl(value);
}

static uint16_t bk7258_wifi_lwip_htons_cb(uint16_t value)
{
  return htons(value);
}

static void bk7258_wifi_net_begin_send_arp_reply_cb(int is_send_arp,
                                                     int is_allow_send_req)
{
  net_begin_send_arp_reply(is_send_arp, is_allow_send_req);
}

/* The adapter ABI predates the RF arbitration API.  The former accepts a
 * legacy command/parameter pair, while BK7258's provider accepts a complete
 * arbitration request.  Preserve the two documented Wi-Fi PLL hold commands
 * and deliberately reject every other legacy command rather than guessing an
 * RF operation. */
static UINT32 bk7258_wifi_rf_pll_ctrl_cb(UINT32 cmd, UINT32 param)
{
  enum RF_PLL_E pll;

  (void)param;

  if (cmd == CMD_RF_WIFIPLL_HOLD_BIT_SET)
    {
      pll = RF_PLL_HIGH;
    }
  else if (cmd == CMD_RF_WIFIPLL_HOLD_BIT_CLR)
    {
      pll = RF_PLL_LOW;
    }
  else
    {
      return (UINT32)RF_ARBIT_RESULT_ERROR;
    }

  return (UINT32)rf_pll_ctrl(MODULE_TYPE_WIFI, RF_OPERATION_APPLY,
                             RF_PATH_WIFI_IQ, pll,
                             RF_PRIORITY_WIFI_NORMAL, true);
}

/* The BK7258 PHY provider consumes a 2.4-GHz center frequency in MHz.  It
 * does not reject arbitrary values above 2400 before encoding freq - 2400 in
 * a hardware field, so validate the ABI boundary before entering it. */
static bool bk7258_wifi_is_valid_2g_freq(UINT32 freq)
{
  return freq == 2484 ||
         (freq >= 2412 && freq <= 2472 && ((freq - 2412) % 5) == 0);
}

static void bk7258_wifi_cal_set_channel_cb(UINT32 freq)
{
  if (!bk7258_wifi_is_valid_2g_freq(freq))
    {
      bk_printf("[BK7258-WIFI] invalid capability argument: "
                "_rwnx_cal_set_channel freq=%lu MHz\n",
                (unsigned long)freq);
      PANIC();
    }

  rwnx_cal_set_channel(freq);
}

/* This symbol exists in the linked PHY archive, but its BK7258 RX-retuning
 * contract has not been established.  Upstream only enables it on BK7236, so
 * preserve a diagnostic trap rather than pretending the operation succeeded. */
/* The authoritative BK7258 initializer binds these slots to the pinned
 * archives' own implementations (bk7011_update_by_rx is a no-op wrapper on
 * BK7258 upstream; restore_all_regs_for_mac is the real MAC-register restore
 * used around mac sleep transitions).  The earlier diagnostic PANIC traps are
 * therefore replaced by the real providers, matching Armino exactly instead
 * of trapping on paths the SDK itself services.  _bk_wifi_stop_rf closes the
 * RF through the same vote controller the SDK uses; STA-only profile always
 * votes with RF_BY_WIFI_BIT. */

extern void restore_all_regs_for_mac(void);
extern void delay05us(INT32 num);
extern void bk7011_update_by_rx(int8_t rssi, int8_t freq_offset);

static void bk7258_wifi_stop_rf_cb(void)
{
  rf_module_vote_ctrl(RF_CLOSE, RF_BY_WIFI_BIT);
}

static void bk7258_wifi_bt_state_notify_disabled_cb(uint8_t is_active)
{
  /* This is notification-only in the selected BT-less profile.  Keep the
   * missing coexistence integration visible without inventing a BT/PTA
   * implementation or blocking the STA-only power transition. */
  bk_printf("[BK7258-WIFI] unsupported capability: "
            "_wifi_notify_state_to_bt state=%u (BT/coex disabled)\n",
            (unsigned int)is_active);
}

/* Real power/clock/RF-control providers for the scan/RF enablement chain.
 *
 * Board evidence (2026-08-28 scan trace): every probe-request TX was released
 * by txl_cntrl_tx_check() with the NX MAC master FSM read as 0/0x0/0x0
 * (0x49100500/0x49100504), so no probe ever reached the air.  The Armino SDK
 * initializer binds _wifi_mac_phy_power_on, _wifi_vote_rf_ctrl,
 * _wifi_phy_clk_open/close and _bk_wifi_set_rf_en to real power/clock/RF
 * providers; this port previously left them unset.  These callbacks bind the
 * pinned BK7258 archives' own implementations (libbk_phy.a rf_cntrl.c.obj /
 * bk7236_cal.c.obj) plus the team sysctrl/clock helpers whose register bits
 * were verified against the authoritative mapping (SYS+0x40 bits 9/10,
 * SYS+0xC bits 26/27).  Keep the owner selected by the caller: the authority
 * routes a zero is_wifi argument to the BLE voter, so collapsing it to Wi-Fi
 * corrupts the PHY clock-vote ownership bitmap. */

static void bk7258_wifi_mac_phy_power_on_cb(void)
{
  /* Matches the authoritative wifi_mac_phy_power_on_wrapper exactly
   * (vendored bk_wifi_adapter.c:426-436): three PM power votes then two
   * clock enables.  The PHY_WIFI submodule vote is NOT redundant -- it
   * drives the PM submodule refcount and, on first power-on, calls
   * phy_wakeup_reinit() inside the closed library.  The previous
   * implementation called bk7258_phy_power() directly, bypassing that
   * state machine entirely. */

  bk_err_t ret;

  ret = bk_pm_module_vote_power_ctrl(PM_POWER_MODULE_NAME_WIFIP_MAC,
                                      PM_POWER_MODULE_STATE_ON);
  if (ret != BK_OK)
    syslog(LOG_ERR, "[BK7258-WIFI] mpo FAIL mac_power ret=%d\n", ret);

  ret = bk_pm_module_vote_power_ctrl(PM_POWER_MODULE_NAME_PHY,
                                      PM_POWER_MODULE_STATE_ON);
  if (ret != BK_OK)
    syslog(LOG_ERR, "[BK7258-WIFI] mpo FAIL phy_power ret=%d\n", ret);

  ret = bk_pm_module_vote_power_ctrl(PM_POWER_SUB_MODULE_NAME_PHY_WIFI,
                                      PM_POWER_MODULE_STATE_ON);
  if (ret != BK_OK)
    syslog(LOG_ERR, "[BK7258-WIFI] mpo FAIL phy_wifi ret=%d\n", ret);

  ret = bk_pm_clock_ctrl(PM_CLK_ID_MAC, PM_CLK_CTRL_PWR_UP);
  if (ret != BK_OK)
    syslog(LOG_ERR, "[BK7258-WIFI] mpo FAIL mac_clock ret=%d\n", ret);

  ret = bk_pm_clock_ctrl(PM_CLK_ID_PHY, PM_CLK_CTRL_PWR_UP);
  if (ret != BK_OK)
    syslog(LOG_ERR, "[BK7258-WIFI] mpo FAIL phy_clock ret=%d\n", ret);
}

static void bk7258_wifi_vote_rf_ctrl_cb(uint8_t cmd)
{
  rf_module_vote_ctrl(cmd, RF_BY_WIFI_BIT);
}

static void bk7258_wifi_phy_clk_open_cb(uint8_t is_wifi)
{
  if (is_wifi)
    {
      phy_clk_open_handler(RF_BY_WIFI_BIT);
    }
  else
    {
      phy_clk_open_handler(RF_BY_BLE_BIT);
    }
}

static void bk7258_wifi_phy_clk_close_cb(uint8_t is_wifi)
{
  if (is_wifi)
    {
      phy_clk_close_handler(RF_BY_WIFI_BIT);
    }
  else
    {
      phy_clk_close_handler(RF_BY_BLE_BIT);
    }
}

/* Blind-spot closure (2026-08-28 full-archive scan): every wifi_os_funcs_t
 * slot the pinned libwifi.a/libbk_phy.a can dereference was cross-checked
 * against the port bindings; the wrappers below close the NULL gaps so no
 * 7236XX-family path can BLX through a zero slot.  Each wrapper copies the
 * authoritative BK7258 profile binding: commented-out bodies stay empty,
 * CKMN returns OK without CONFIG_CKMN, WAPI stays NULL exactly as upstream.
 * _send_udp_bc_pkt (airkiss) and the low-analog pair have no provider in
 * this profile; they log once and return instead of pretending success. */

static void power_save_delay_sleep_check_cb(void) { }
static void power_save_wake_mac_rf_if_in_sleep_cb(void) { }
static void power_save_wake_mac_rf_end_clr_flag_cb(void) { }
static UINT8 power_save_if_ps_rf_dtim_enabled_cb(void) { return 0; }
static UINT16 power_save_forbid_trace_cb(UINT16 forbid)
{
  (void)forbid;
  return 0;
}
static int ps_need_pre_process_cb(UINT32 arg)
{
  (void)arg;
  return 0;
}
static bool power_save_rf_sleep_check_cb(void) { return false; }
static void mac_ps_bcn_callback_cb(uint8_t *data, int len)
{
  (void)data;
  (void)len;
}
static UINT8 mac_sleeped_cb(void) { return 0; }
static bk_err_t bk_ckmn_driver_get_rc32k_ppm_cb(void) { return BK_OK; }
static UINT32 mcu_ps_machw_cal_cb(void) { return 0; }
static UINT32 mcu_ps_machw_reset_cb(void) { return 0; }
static UINT32 mcu_ps_machw_init_cb(void) { return 0; }
static void mcu_ps_bcn_callback_cb(uint8_t *data, int len)
{
  (void)data;
  (void)len;
}
static void dbg_enable_debug_gpio_cb(void) { }
static bk_err_t bk7258_wifi_gpio_unmap_cb(uint32_t id)
{
  return gpio_dev_unmap((gpio_id_t)id);
}
static bk_err_t bk7258_wifi_gpio_map_cb(uint32_t id, uint32_t dev)
{
  return gpio_dev_map((gpio_id_t)id, (gpio_dev_t)dev);
}
/* Transcribed from the authority's cache driver semantics
 * (cp/middleware/arch/cm33/cache.c:37-42): flush = Clean + Invalidate over the
 * whole D-cache.  The old body here was an empty no-op from the pre-cache era;
 * the port now runs with CONFIG_ARCH_DCACHE=y, and with the slot empty the
 * closed library gets no writeback before whichever DMA handoff it guards --
 * exactly the "silent data corruption on the MAC data path" our own
 * hal_port/include/cache.h:12-16 warns about.  NuttX exposes the two halves
 * separately (arm_cache.c:895/:737), so the authority's single
 * CleanInvalidateDCache is spelled as clean-then-invalidate; the net cache
 * state is the same (every line written back and invalidated). */
static void flush_all_dcache_cb(void)
{
  up_clean_dcache_all();
  up_invalidate_dcache_all();
}
static uint32_t bk7258_wifi_udp_bc_pkt_cb(u8 random_data)
{
  static bool reported;
  if (!reported) { reported = true;
    bk_printf("[BK7258-WIFI] capability: _send_udp_bc_pkt (airkiss) not in profile\n"); }
  (void)random_data;
  return 0;
}
extern void bk7258_wifi_pwd_ofdm_override(uint32_t v);
extern uint32_t bk7258_wifi_pwd_ofdm_get_override(void);
extern void sys_hal_enter_low_analog(void);
extern void sys_hal_exit_low_analog(void);

static void bk7258_wifi_enter_low_analog_cb(void)
{
  /* Authority sys_pm_hal.c:1281, imported under pm/authority.  Was a local
   * hp_ hand-port until 2026-09-09. */
  sys_hal_enter_low_analog();
  return;
}
#if 0
static void bk7258_wifi_enter_low_analog_cb_unused(void)
{
  static bool reported;
  if (!reported) { reported = true;
    bk_printf("[BK7258-WIFI] capability: _sys_hal_enter_low_analog not in profile\n"); }
}
#endif

static void bk7258_wifi_exit_low_analog_cb(void)
{
  sys_hal_exit_low_analog();
  return;
}
#if 0
static void bk7258_wifi_exit_low_analog_cb_unused(void)
{
  static bool reported;
  if (!reported) { reported = true;
    bk_printf("[BK7258-WIFI] capability: _sys_hal_exit_low_analog not in profile\n"); }
}
#endif
extern uint32_t hp_rc_drv_get_rx_mode_enrxsw(void);
extern void hp_rc_drv_set_rx_mode_enrxsw(uint32_t en);
extern void hp_rc_drv_set_agc_manual_en(uint32_t en);
extern int hp_os_vsnprintf(char *buf, uint32_t size, const char *fmt, va_list ap);
extern int hp_net_wlan_add_netif(uint8_t vif_idx);
extern int hp_net_wlan_remove_netif(uint8_t vif_idx);
extern int hp_sta_ip_start(void);
extern int hp_sta_ip_down(void);
extern int hp_sta_ip_mode_set(uint32_t mode);
extern int hp_uap_ip_start(void);
extern int hp_uap_ip_down(void);
extern char *hp_inet_ntoa(uint32_t ip);
extern uint32_t hp_lookup_ipaddr(const char *name);
extern void hp_get_net_info(void *info);
extern void hp_save_net_info(void *netif, void *sta);
extern int hp_set_sta_status(uint32_t status);
extern void hp_do_evm(void *param);
extern void hp_do_rx_sensitivity(void);
extern void hp_evm_via_mac_evt(void);
extern void hp_evm_via_mac_continue(void);
extern uint32_t hp_tx_evm_rate_get(void);
extern uint32_t hp_tx_evm_bandwidth_get(void);
extern uint32_t hp_tx_evm_mode_get(void);
extern uint32_t hp_tx_evm_guard_i_tpye_get(void);
extern uint32_t hp_tx_evm_modul_format_get(void);
extern uint32_t hp_tx_evm_pwr_idx_get(void);
extern int hp_wapi_wpi_encrypt(void *param);
extern int hp_wapi_wpi_decrypt(void *param);
extern uint32_t hp_get_pbuf_pool_size(void);
extern uint32_t hp_get_rx_pbuf_type(void);

static void tx_verify_test_call_back_cb(void) { }

extern bk_err_t bk_wifi_get_vendor_ie_cb_internal(void *vendor_ie, uint32_t vendor_type, uint16_t len, uint8_t frame_type);
extern uint32_t bk_wifi_get_vendor_ie_type(void);
extern uint8_t bk_wifi_get_vendor_ie_oui_len(void);
extern void bk_wifi_csi_info_cb(void *data);

/* ABI-complete objects: designated initializers preserve the generated
 * layout while leaving unported P3-P5 callbacks visibly NULL. Runtime startup
 * remains disabled until those rows are replaced by real providers. */

/* Adjacency pins the RF/power callback group exactly as the generated
 * adapter header lays it out (vote/clk-open/clk-close/mac-power-on), so any
 * regeneration that reorders them breaks the link instead of silently
 * misbinding the pinned providers. */
_Static_assert(offsetof(wifi_os_funcs_t, _wifi_phy_clk_open)
               == offsetof(wifi_os_funcs_t, _wifi_vote_rf_ctrl) + 4,
               "libwifi.a ABI: PHY clock-open offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _wifi_phy_clk_close)
               == offsetof(wifi_os_funcs_t, _wifi_phy_clk_open) + 4,
               "libwifi.a ABI: PHY clock-close offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _wifi_mac_phy_power_on)
               == offsetof(wifi_os_funcs_t, _wifi_phy_clk_close) + 4,
               "libwifi.a ABI: MAC/PHY power-on offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _bk_wifi_set_rf_en) > 0,
               "libwifi.a ABI: RF-enable callback missing");

_Static_assert(offsetof(wifi_os_funcs_t, _bk_feature_get_mac_sup_sta_max_num)
               == 0x204,
               "libwifi.a ABI: station-count callback offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _sys_is_enable_hw_tpc) == 0x1c,
               "libwifi.a ABI: hardware-TPC callback offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _bk_feature_send_deauth_before_connect)
               == 0x1dc,
               "libwifi.a ABI: send-deauth callback offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _rtos_disable_int) == 0x230,
               "libwifi.a ABI: IRQ-mask callback offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _register_wifi_dump_hook) == 0x2e0,
               "libwifi.a ABI: dump-hook callback offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _tpc_change_pwr_by_media) == 0x340,
               "libwifi.a ABI: media-TPC changer offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _tpc_set_media_pwr_level) == 0x344,
               "libwifi.a ABI: media-TPC setter offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _tpc_get_media_pwr_level) == 0x348,
               "libwifi.a ABI: media-TPC getter offset changed");
_Static_assert(offsetof(wifi_os_funcs_t, _rwnx_cal_recover_rcbeken_reg_val)
               == 0x84,
               "libwifi.a ABI: PHY calibration-restore callback offset changed");
_Static_assert(offsetof(phy_os_funcs_t, _aon_pmu_hal_get_chipid) == 0x84,
               "libbk_phy.a ABI: PHY AON chip-ID callback offset changed");
_Static_assert(offsetof(phy_os_funcs_t, _sys_ll_get_ana_reg5_adc_div) == 0x94,
               "libbk_phy.a ABI: PHY ANA5 ADC-divider callback offset changed");

wifi_os_funcs_t g_wifi_os_funcs =
{
  ._version = BK_WIFI_OS_ADAPTER_VERSION,
  /* RF capability entries below are backed by the linked BK PHY/runtime
   * implementation; do not replace them with synthetic success values. */
  ._manual_cal_rfcali = manual_cal_rfcali_status,
  /* 权威不填此槽。函数本体在 libbk_phy.a 里（两边同有），库需要时直调即可；
   * 挂表只会激活权威没有的表内路径。退回 NULL 与权威逐槽相等。 */
  //._rc_drv_set_rf_en = rc_drv_set_rf_en,
  ._rc_drv_get_rf_en = rc_drv_get_rf_en,
  /* Scan-probe TX chain requires real RF/power control; see the provider
   * block above for the board evidence that motivated these bindings. */
  ._wifi_vote_rf_ctrl = bk7258_wifi_vote_rf_ctrl_cb,
  ._wifi_phy_clk_open = bk7258_wifi_phy_clk_open_cb,
  ._wifi_phy_clk_close = bk7258_wifi_phy_clk_close_cb,
  ._wifi_mac_phy_power_on = bk7258_wifi_mac_phy_power_on_cb,
  ._bk_wifi_set_rf_en = bk_wifi_set_rf_en,
  ._bk_wifi_stop_rf = bk7258_wifi_stop_rf_cb,
  ._delay05us = delay05us,
  /* blind-spot closure bindings, authoritative semantics per wrapper */
  ._power_save_delay_sleep_check = power_save_delay_sleep_check_cb,
  ._power_save_wake_mac_rf_if_in_sleep = power_save_wake_mac_rf_if_in_sleep_cb,
  ._power_save_wake_mac_rf_end_clr_flag = power_save_wake_mac_rf_end_clr_flag_cb,
  ._power_save_if_ps_rf_dtim_enabled = power_save_if_ps_rf_dtim_enabled_cb,
  ._power_save_forbid_trace = power_save_forbid_trace_cb,
  ._ps_need_pre_process = ps_need_pre_process_cb,
  ._power_save_rf_sleep_check = power_save_rf_sleep_check_cb,
  ._mac_ps_bcn_callback = mac_ps_bcn_callback_cb,
  ._mac_sleeped = mac_sleeped_cb,
  ._bk_ckmn_driver_get_rc32k_ppm = bk_ckmn_driver_get_rc32k_ppm_cb,
  ._dbg_enable_debug_gpio = dbg_enable_debug_gpio_cb,
  ._gpio_dev_unprotect_unmap = bk7258_wifi_gpio_unmap_cb,
  ._gpio_dev_unprotect_map = bk7258_wifi_gpio_map_cb,
  ._mcu_ps_machw_cal = mcu_ps_machw_cal_cb,
  ._mcu_ps_machw_reset = mcu_ps_machw_reset_cb,
  ._mcu_ps_machw_init = mcu_ps_machw_init_cb,
  ._mcu_ps_bcn_callback = mcu_ps_bcn_callback_cb,
  ._bk_feature_csi_out_cb = bk_wifi_csi_info_cb,
  ._bk_wifi_get_vendor_ie_cb_internal = bk_wifi_get_vendor_ie_cb_internal,
  ._bk_wifi_get_vendor_ie_type = bk_wifi_get_vendor_ie_type,
  ._bk_wifi_get_vendor_ie_oui_len = bk_wifi_get_vendor_ie_oui_len,
  ._flush_all_dcache = flush_all_dcache_cb,
  ._send_udp_bc_pkt = bk7258_wifi_udp_bc_pkt_cb,
  ._tx_verify_test_call_back = tx_verify_test_call_back_cb,
  ._sys_hal_enter_low_analog = bk7258_wifi_enter_low_analog_cb,
  ._sys_hal_exit_low_analog = bk7258_wifi_exit_low_analog_cb,
  ._cal_set_rfconfig_BTPLL = rwnx_cal_set_rfconfig_BTPLL,
  ._cal_set_rfconfig_WIFIPLL = rwnx_cal_set_rfconfig_WIFIPLL,
  ._calibration_init = calibration_init,
  ._sys_is_enable_hw_tpc = rwnx_sys_is_enable_hw_tpc,
  ._rwnx_cal_save_trx_rcbekn_reg_val = rwnx_cal_save_trx_rcbekn_reg_val,
  ._rwnx_tpc_get_pwridx_by_rate = rwnx_tpc_get_pwridx_by_rate,
  ._rwnx_is_enable_pwr_change_by_rssi = rwnx_is_enable_pwr_change_by_rssi,
  ._tpc_auto_change_pwr_by_rssi = tpc_auto_change_pwr_by_rssi,
  /* BK7258 is a BK7236-family part, so Armino's CONFIG_SOC_BK7236XX branch
   * binds this exact media-TPC trio.  These slots are not optional for the
   * active authority configuration. */
  ._tpc_change_pwr_by_media = tpc_change_pwr_by_media,
  ._tpc_set_media_pwr_level = tpc_set_media_pwr_level,
  ._tpc_get_media_pwr_level = tpc_get_media_pwr_level,
  ._bk7011_max_rxsens_setting = bk7011_max_rxsens_setting,
  ._bk7011_default_rxsens_setting = bk7011_default_rxsens_setting,
  ._rwnx_setting_for_single_rate = rwnx_setting_for_single_rate,
  ._rwnx_cal_recover_rf_setting = rwnx_cal_recover_rf_setting,
  ._rwnx_cal_recover_wifi_setting = rwnx_cal_recover_wifi_setting,
  ._rwnx_cal_set_40M_setting = rwnx_cal_set_40M_setting,
  ._rwnx_cal_set_20M_setting = rwnx_cal_set_20M_setting,
  ._rwnx_cal_recover_rcbeken_reg_val = rwnx_cal_recover_rcbeken_reg_val,
  ._rwnx_cal_recover_trx_reg_val = rwnx_cal_recover_trx_reg_val,
  ._rwnx_cal_set_channel = bk7258_wifi_cal_set_channel_cb,
  ._bk7011_cal_pll = bk7011_cal_pll,
  ._bk7011_update_by_rx = bk7011_update_by_rx,
  ._rwnx_cal_load_trx_rcbekn_reg_val = rwnx_cal_load_trx_rcbekn_reg_val,
  ._manual_get_epa_flag = manual_get_epa_flag,
  ._rxsens_start_flag_get = rxsens_start_flag_get,
  ._os_malloc = bk7258_wifi_os_malloc_cb,
  ._os_zalloc = bk7258_wifi_os_zalloc_cb,
  ._os_free = bk7258_wifi_os_free_cb,
  ._rtos_realloc = bk7258_wifi_os_realloc_cb,
  ._os_memcmp = bk7258_wifi_memcmp_cb,
  ._os_memset = bk7258_wifi_memset_cb,
  ._os_memcpy = bk7258_wifi_memcpy_cb,
  ._os_memmove = bk7258_wifi_memmove_cb,
  ._os_strlen = bk7258_wifi_strlen_cb,
  ._os_strcmp = bk7258_wifi_strcmp_cb,
  ._os_strncmp = bk7258_wifi_strncmp_cb,
  ._os_strcpy = bk7258_wifi_strcpy_cb,
  ._os_strncpy = bk7258_wifi_strncpy_cb,
  ._os_strchr = bk7258_wifi_strchr_cb,
  ._os_strrchr = bk7258_wifi_strrchr_cb,
  ._os_strstr = bk7258_wifi_strstr_cb,
  ._os_strdup = bk7258_wifi_strdup_cb,
  ._os_strlcpy = bk7258_wifi_strlcpy_cb,
  ._os_snprintf = bk7258_wifi_snprintf_cb,
  ._os_strtoul = bk7258_wifi_strtoul_cb,
  ._os_strcasecmp = bk7258_wifi_strcasecmp_cb,
  ._os_strncasecmp = bk7258_wifi_strncasecmp_cb,
  ._ate_is_enabled = ate_is_enabled,
  ._bk_feature_receive_bcmc_enable = bk_feature_receive_bcmc_enable,
  ._bk_feature_bssid_connect_enable = bk_feature_bssid_connect_enable,
  ._bk_feature_fast_connect_enable = bk_feature_fast_connect_enable,
  ._bk_feature_send_deauth_before_connect =
    bk_feature_send_deauth_before_connect,
  ._bk_feature_fast_dhcp_enable = bk_feature_fast_dhcp_enable,
  ._bk_feature_get_scan_speed_level = bk_feature_get_scan_speed_level,
  ._bk_feature_not_check_ssid_enable = bk_feature_not_check_ssid_enable,
  ._bk_feature_config_cache_enable = bk_feature_config_cache_enable,
  ._bk_feature_config_ckmn_enable = bk_feature_ckmn_enable,
  ._bk_feature_sta_vsie_enable = bk_feature_sta_vsie_enable,
  ._bk_feature_ap_statype_limit_enable = bk_feature_ap_statype_limit_enable,
  ._bk_feature_close_coexist_csa = bk_feature_close_coexist_csa,
  ._bk_feature_network_found_event = bk_feature_network_found_event,
  ._bk_feature_get_mac_sup_sta_max_num =
    bk_feature_get_mac_sup_sta_max_num,
  ._sys_drv_modem_bus_clk_ctrl = sys_drv_modem_bus_clk_ctrl,
  ._sys_drv_modem_clk_ctrl = sys_drv_modem_clk_ctrl,
  ._sys_drv_int_enable = bk7258_wifi_sys_drv_int_enable_cb,
  ._sys_drv_int_disable = bk7258_wifi_sys_drv_int_disable_cb,
  ._sys_drv_int_group2_enable = bk7258_wifi_sys_drv_int_group2_enable_cb,
  ._sys_drv_int_group2_disable = bk7258_wifi_sys_drv_int_group2_disable_cb,
  ._sys_ll_set_cpu_power_sleep_wakeup_pwd_ofdm =
    bk7258_wifi_pwd_ofdm_override,
  ._sys_ll_get_cpu_power_sleep_wakeup_pwd_ofdm =
    bk7258_wifi_pwd_ofdm_get_override,
  ._sys_ll_get_cpu_device_clk_enable_mac_cken =
    sys_ll_get_cpu_device_clk_enable_mac_cken,
  ._sys_ll_get_cpu_device_clk_enable_phy_cken =
    sys_ll_get_cpu_device_clk_enable_phy_cken,
  ._sys_drv_module_power_state_get =
    bk7258_wifi_sys_drv_module_power_state_get_cb,
  ._bk_pm_module_vote_sleep_ctrl = bk7258_wifi_pm_vote_sleep_cb,
  ._bk_pm_lpo_src_get = bk7258_wifi_pm_lpo_src_cb,
  ._bk_pm_module_power_state_get = bk7258_wifi_pm_module_power_state_cb,
  ._bk_pm_module_vote_power_ctrl = bk7258_wifi_pm_vote_power_cb,
  ._bk_pm_module_vote_cpu_freq = bk7258_wifi_pm_vote_cpu_freq_cb,
  ._bk_pm_phy_reinit_flag_get = bk_pm_phy_reinit_flag_get,
  ._bk_pm_phy_reinit_flag_clear = bk_pm_phy_reinit_flag_clear,
  ._bk_pm_sleep_register = bk_pm_sleep_register_wrapper,
  ._bk_pm_low_voltage_register = bk_pm_low_voltage_register_wrapper,
  ._mac_ps_exc32_cb_notify = bk7258_wifi_mac_ps_exc32_notify_cb,
  ._mac_ps_exc32_init = bk7258_wifi_mac_ps_exc32_init_cb,
  ._wifi_32k_src_switch_ready_notify =
    bk7258_wifi_32k_ready_register_cb,
  ._rwnx_cal_mac_sleep_rc_clr = rwnx_cal_mac_sleep_rc_clr,
  ._rwnx_cal_mac_sleep_rc_recover = rwnx_cal_mac_sleep_rc_recover,
  ._rtos_get_time = bk7258_wifi_time_cb,
  ._rtos_enter_critical = bk7258_wifi_enter_critical_cb,
  ._rtos_exit_critical = bk7258_wifi_exit_critical_cb,
  ._rtos_delay_milliseconds = bk7258_wifi_delay_ms_cb,
  /* ._delay 曾填 bk7258_wifi_delay_cb（按毫秒睡）。退回 NULL 以与权威逐槽
   * 相等：权威 adapter 不填此槽且实测可用，故库对它必然判空跳过 —— 填了反而
   * 激活权威从未走过的路径，且参数语义（ms? tick?）无从考证（无权威语义可抄）。
   * 风险场景：表调用在 core 线程上执行，若闭源库用它等待，我们的 ms 阻塞会
   * 停摆整个 core 线程、CFM 得不到处理 —— 与未解释的间歇 rw_msg_send 超时
   * （reqid 1/6173/7169/7172）形态吻合。退回 NULL 后该嫌疑整体消失。
   * bk7258_wifi_delay_cb 本体保留（可能有别的引用），不再挂表。 */
  //._delay = bk7258_wifi_delay_cb,
  ._rc_drv_get_rx_mode_enrxsw = hp_rc_drv_get_rx_mode_enrxsw,
  ._rc_drv_set_rx_mode_enrxsw = hp_rc_drv_set_rx_mode_enrxsw,
  ._rc_drv_set_agc_manual_en = hp_rc_drv_set_agc_manual_en,
  ._net_wlan_add_netif = hp_net_wlan_add_netif,
  ._net_wlan_remove_netif = hp_net_wlan_remove_netif,
  /* 权威不填此槽（lwIP 层拥有 sta_ip_start，库不走表）。本 port 的对应义务
   * 由 plan §11.4.3 承担：连接成功 → runtime app 侧 carrier_on + DHCPC，
   * 与这个槽无关。hp_sta_ip_start 的 no-op 本体保留。退回 NULL 与权威逐槽
   * 相等，避免激活权威从未走过的表内路径。 */
  //._sta_ip_start = hp_sta_ip_start,
  ._sta_ip_down = hp_sta_ip_down,
  ._sta_ip_mode_set = hp_sta_ip_mode_set,
  ._uap_ip_start = hp_uap_ip_start,
  ._uap_ip_down = hp_uap_ip_down,
  ._inet_ntoa = hp_inet_ntoa,
  ._lookup_ipaddr = hp_lookup_ipaddr,
  ._get_net_info = hp_get_net_info,
  ._save_net_info = hp_save_net_info,

  /* ._set_sta_status intentionally NOT set here -- see the note at the
   * ._set_sta_status assignment further down.  It was assigned twice in this
   * same initializer and C silently keeps the LAST one, so this line was dead
   * while looking live. */

  ._do_evm = hp_do_evm,
  ._do_rx_sensitivity = hp_do_rx_sensitivity,
  ._evm_via_mac_evt = hp_evm_via_mac_evt,
  ._evm_via_mac_continue = hp_evm_via_mac_continue,
  ._tx_evm_rate_get = hp_tx_evm_rate_get,
  ._tx_evm_bandwidth_get = hp_tx_evm_bandwidth_get,
  ._tx_evm_mode_get = hp_tx_evm_mode_get,
  ._tx_evm_guard_i_tpye_get = hp_tx_evm_guard_i_tpye_get,
  ._tx_evm_modul_format_get = hp_tx_evm_modul_format_get,
  ._tx_evm_pwr_idx_get = hp_tx_evm_pwr_idx_get,
  ._wapi_wpi_encrypt = hp_wapi_wpi_encrypt,
  ._wapi_wpi_decrypt = hp_wapi_wpi_decrypt,

  /* ._get_pbuf_pool_size / ._get_rx_pbuf_type intentionally NOT set here.
   *
   * DUPLICATE-INITIALIZER BUG, fixed 2026-09-01: both were assigned here AND
   * again below (to the bk_*_wrapper pair).  In a single initializer C keeps
   * the LAST assignment and GCC says nothing by default (-Woverride-init is
   * off), so these two lines were dead code that read as if the hal_port
   * implementations were in use.  They were also wrong: hp_get_rx_pbuf_type
   * returned lwIP's PBUF_RAM (a 0x0380 bit combination) where the slot's
   * contract is a bk_pbuf_type ordinal 0..4.  The surviving pair in
   * hal_port/pbuf_shim.c now returns BK_PBUF_RAM_RX, matching the authority.
   *
   * Checked at the same time: g_wifi_os_variable has no duplicates. */

  ._delay_us = bk7258_wifi_delay_us_cb,
  ._rtos_lock_mutex = bk7258_wifi_lock_mutex_cb,
  ._rtos_unlock_mutex = bk7258_wifi_unlock_mutex_cb,
  ._bk_ms_to_ticks = bk7258_wifi_ms_to_ticks_cb,
  ._dma_memcpy = bk7258_wifi_dma_memcpy_cb,
  ._rtos_get_ms_per_tick = bk7258_wifi_ms_per_tick_cb,
  /* KE calls this pair before its generic critical-section callbacks. Both
   * save and restore NuttX's interrupt mask through the same OSAL contract. */
  ._rtos_disable_int = bk7258_wifi_enter_critical_cb,
  ._rtos_enable_int = bk7258_wifi_exit_critical_cb,
  ._bk_printf = bk_printf,
  ._bk_null_printf = bk_null_printf,
  ._log_raw = bk_printf_raw,
  ._bk_wifi_interrupt_init = bk_wifi_interrupt_init,
  ._bk_int_isr_register = bk7258_wifi_isr_register_cb,
  ._rtos_push_to_queue = bk7258_wifi_queue_send_cb,
  ._rtos_is_queue_full = bk7258_wifi_queue_full_cb,
  ._rtos_is_queue_empty = bk7258_wifi_queue_empty_cb,
  ._rtos_pop_from_queue = bk7258_wifi_queue_recv_cb,
  ._rtos_push_to_queue_front = bk7258_wifi_queue_front_cb,
  ._bk_aon_rtc_get_current_tick = bk7258_wifi_aon_rtc_tick_cb,
  ._rtos_init_queue = bk7258_wifi_queue_init_cb,
  ._rtos_deinit_queue = bk7258_wifi_queue_deinit_cb,
  ._rtos_init_semaphore = bk7258_wifi_sem_init_cb,
  ._rtos_get_semaphore = bk7258_wifi_sem_wait_cb,
  ._rtos_deinit_semaphore = bk7258_wifi_sem_deinit_cb,
  ._rtos_set_semaphore = bk7258_wifi_sem_post_cb,
  ._rtc_get_ms_tick_cnt = bk7258_wifi_rtc_ms_tick_cb,
  ._rtos_create_thread = bk7258_wifi_thread_create_cb,
  ._rtos_delete_thread = bk7258_wifi_thread_delete_cb,
  ._rtos_get_free_heap_size = rtos_get_free_heap_size,
  ._rtos_init_mutex = bk7258_wifi_mutex_init_cb,
  ._rtos_deinit_mutex = bk7258_wifi_mutex_deinit_cb,
  ._rtos_reload_timer = bk7258_wifi_timer_reload_cb,
  ._rtos_is_timer_running = bk7258_wifi_timer_running_cb,
  ._rtos_stop_timer = bk7258_wifi_timer_stop_cb,
  ._rtos_init_timer = bk7258_wifi_timer_init_cb,
  ._rtos_is_current_thread = bk7258_wifi_is_current_thread_cb,
  ._register_wifi_dump_hook = bk7258_wifi_register_dump_hook_cb,
  ._rf_pll_ctrl = bk7258_wifi_rf_pll_ctrl_cb,
  ._rw_msg_send = rw_msg_send,
  ._rw_ieee80211_init_scan_chan = rw_ieee80211_init_scan_chan,
  ._rw_ieee80211_get_scan_default_chan_num =
    rw_ieee80211_get_scan_default_chan_num,
  ._rwnx_set_bk_rlk_start = rwnx_set_wifi_rlk_start,
  ._sr_get_scan_number = sr_get_scan_number,
  ._sr_get_scan_results = sr_get_scan_results,
  ._sr_flush_scan_results = sr_flush_scan_results,
  ._rwnx_get_rlk_info_results = rwnx_get_rlk_info_results,
  ._rwnx_flush_rlk_info_results = rwnx_flush_rlk_info_results,
  ._wifi_notify_state_to_bt = bk7258_wifi_bt_state_notify_disabled_cb,
  ._bk_restore_all_regs_for_mac = restore_all_regs_for_mac,
  ._pbuf_alloc = bk_pbuf_alloc_wrapper,
  ._pbuf_free = bk_pbuf_free_wrapper,
  ._pbuf_ref = bk_pbuf_ref_wrapper,
  ._pbuf_header = bk_pbuf_header_wrapper,
  ._pbuf_cat = bk_pbuf_cat_wrapper,
  ._pbuf_coalesce = bk_pbuf_coalesce_wrapper,
  ._get_rx_pbuf_type = bk_get_rx_pbuf_type_wrapper,
  ._get_pbuf_pool_size = bk_get_pbuf_pool_size_wrapper,
  ._get_netif_hostname = bk7258_wifi_get_netif_hostname_cb,
  ._lwip_ntohl = bk7258_wifi_lwip_ntohl_cb,
  ._lwip_htons = bk7258_wifi_lwip_htons_cb,
  ._set_sta_status = bk7258_wifi_set_sta_status_cb,
  ._net_begin_send_arp_reply = bk7258_wifi_net_begin_send_arp_reply_cb,
  ._mac_printf_encode = bk7258_wifi_mac_printf_encode_cb,
  ._shell_assert_out = bk7258_wifi_shell_assert_out_cb,
  ._rtos_assert = bk7258_wifi_rtos_assert_cb,
  ._log = bk_printf_ext,
  ._bk_task_wdt_feed = bk_task_wdt_feed,
};

/*
 * This table is an ABI input to the pinned libwifi.a.  Use designated
 * initializers so its generated layout remains the source of truth and so a
 * future ABI expansion is visibly reviewed.  The zero-valued rows below are
 * intentional: BK7258 has no verified equivalent for the debug register,
 * low-power settling delays, or HSU interrupt routing.  They must not be
 * inferred from a neighbouring Beken SoC.
 */
wifi_os_variable_t g_wifi_os_variable =
{
  ._sys_drv_clk_on = SYS_DRV_CLK_ON,

  ._ps_forbid_in_doze = PS_FORBID_IN_DOZE,
  ._ps_forbid_txing = PS_FORBID_TXING,
  ._ps_forbid_hw_timer = PS_FORBID_HW_TIMER,
  ._ps_forbid_vif_prevent = PS_FORBID_VIF_PREVENT,
  ._ps_forbid_keevt_on = PS_FORBID_KEEVT_ON,
  ._ps_bmsg_iotcl_rf_timer_init = PS_BMSG_IOCTL_RF_PS_TIMER_INIT,
  ._ps_bmsg_ioctl_rf_ps_timer_deint = PS_BMSG_IOCTL_RF_PS_TIMER_DEINIT,
  ._ps_bmsg_ioctl_rf_enable = PS_BMSG_IOCTL_RF_ENABLE,
  ._ps_bmsg_ioctl_mcu_enable = PS_BMSG_IOCTL_MCU_ENABLE,
  ._ps_bmsg_ioctl_mcu_disable = PS_BMSG_IOCTL_MCU_DISANABLE,
  ._ps_bmsg_ioctl_ps_enable = PS_BMSG_IOCTL_PS_ENABLE,
  ._ps_bmsg_ioctl_ps_disable = PS_BMSG_IOCTL_PS_DISANABLE,
  ._ps_bmsg_ioctl_exc32k_start = PS_BMSG_IOCTL_EXC32K_START,
  ._ps_bmsg_ioctl_exc32k_stop = PS_BMSG_IOCTL_EXC32K_STOP,

  ._pm_lpo_src_divd = PM_LPO_SRC_DIVD,
  ._pm_lpo_src_x32k = PM_LPO_SRC_X32K,
  ._pm_lpo_src_rosc = PM_LPO_SRC_ROSC,
  ._pm_power_module_name_btsp = PM_POWER_MODULE_NAME_BTSP,
  ._pm_power_module_name_wifip_mac = PM_POWER_MODULE_NAME_WIFIP_MAC,
  ._pm_power_module_name_phy = PM_POWER_MODULE_NAME_PHY,
  ._pm_power_module_state_off = PM_POWER_MODULE_STATE_OFF,
  ._pm_power_module_state_on = PM_POWER_MODULE_STATE_ON,
  ._pm_power_sub_module_name_phy_wifi = PM_POWER_SUB_MODULE_NAME_PHY_WIFI,
  ._pm_sleep_module_name_wifip_mac = PM_SLEEP_MODULE_NAME_WIFIP_MAC,
  ._pm_dev_id_mac = PM_DEV_ID_MAC,
  ._pm_cpu_frq_60m = PM_CPU_FRQ_60M,
  ._pm_cpu_frq_80m = PM_CPU_FRQ_80M,
  ._pm_cpu_frq_120m = PM_CPU_FRQ_120M,
  ._pm_cpu_frq_high = PM_CPU_FRQ_480M,
  ._pm_cpu_frq_default = PM_CPU_FRQ_60M,
  ._pm_32k_step_begin = PM_32K_STEP_BEGIN,
  ._pm_32k_step_finish = PM_32K_STEP_FINISH,

  /* Low-power clock-switch timing constants (values from the
   * authoritative bk_wifi_adapter.c / bk7258 sys_types.h):
   *  xtal_dpll_stability = (0.29ms DPLL + 0.75ms restore) * 1000 = 1040
   *  hardware delay = 500us; 26M stability = 1800us (project config).
   * The library consumes these during sleep/wake clock transitions;
   * leaving them unbound left the timing at 0. */
  ._low_power_xtal_dpll_stability_delay_time = 1040,
  ._low_power_delay_time_hardware = 500,
  ._low_power_26m_stability_delay_time_hardware = 1800,
  /* WIFI_HSU interrupt enable bit: CPU0_INT_32_63_EN bit0 (WIFI_HSU). */
  ._wifi_hsu_interrupt_ctrl_bit = 1,
  /* Low-voltage wakeup lead time and debug config register address. */
  /* 0 -> 188: the authority fills
   * PM_LOW_VOLTAGE_DELTA_WAKEUP_DELAY_IN_US = ceil(DELTA*1e6/RTC_CLOCK_FREQ),
   * DELTA = (0xb+0xb+3)-(8+8+3) = 6 (cp/middleware/soc/bk7258/hal/sys_types.h
   * :115-130), RTC_CLOCK_FREQ = 32000 in this profile -> 188.  The 0 was
   * flagged in the value-table work as strictly worse than the real number
   * ("tells the library the wakeup delay is zero"); the pinned libwifi.a is a
   * PM_V2=1 build and may read this slot, so it gets the authority's number,
   * not a guess. */
  ._pm_low_voltage_delta_wakeup_delay_in_us = 188,
  ._sys_sys_debug_config1_addr = 0x44010000 + (0x39 << 2),

  ._cmd_rf_wifipll_hold_bit_set = CMD_RF_WIFIPLL_HOLD_BIT_SET,
  ._cmd_rf_wifipll_hold_bit_clr = CMD_RF_WIFIPLL_HOLD_BIT_CLR,
  ._rf_wifipll_hold_by_wifi_bit = RF_WIFIPLL_HOLD_BY_WIFI_BIT,

  ._wifi_modem_en = WIFI_MODEM_EN,
  ._wifi_modem_rc_en = WIFI_MODEM_RC_EN,
  ._wifi_mac_tx_rx_timer_int_bit = WIFI_MAC_TX_RX_TIMER_INT_BIT,
  ._wifi_mac_tx_rx_misc_int_bit = WIFI_MAC_TX_RX_MISC_INT_BIT,
  ._wifi_mac_rx_trigger_int_bit = WIFI_MAC_RX_TRIGGER_INT_BIT,
  ._wifi_mac_tx_trigger_int_bit = WIFI_MAC_TX_TRIGGER_INT_BIT,
  ._wifi_mac_port_trigger_int_bit = WIFI_MAC_PORT_TRIGGER_INT_BIT,
  ._wifi_mac_gen_int_bit = WIFI_MAC_GEN_INT_BIT,
  ._wifi_mac_wakeup_int_bit = WIFI_MAC_WAKEUP_INT_BIT,
  ._int_src_mac_general = INT_SRC_MAC_GENERAL,
  ._int_src_mac_rx_trigger = INT_SRC_MAC_RX_TRIGGER,
  ._int_src_mac_txrx_timer = INT_SRC_MAC_TXRX_TIMER,
  ._int_src_mac_prot_trigger = INT_SRC_MAC_PROT_TRIGGER,
  ._int_src_mac_tx_trigger = INT_SRC_MAC_TX_TRIGGER,
  ._int_src_modem = INT_SRC_MODEM,
  ._int_src_modem_rc = INT_SRC_MODEM_RC,

  ._improve_he_tb_enable = false,
  ._ble_polar_enable = false,
};

void bk7258_wifi_adapter_bind(void)
{
  g_wifi_funcs = &g_wifi_os_funcs;
  g_wifi_vars = &g_wifi_os_variable;
}

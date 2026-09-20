/* Wi-Fi-only translation unit: establish Armino's local build contract. */
#include <bk_prelude.h>

/* TEMPORARY BRING-UP DIAGNOSTIC -- REMOVE with the rest of the "[initseq]"
 * instrumentation; see hal_port/include/bk7258_initseq.h for the rationale and the
 * full removal recipe.
 *
 * bk_wifi_init() stops emitting output after calibration_init()'s last
 * "[cal] idx:41=..." line and never returns.  Only a call that blocks ON THIS
 * THREAD can do that, so each such call in wifi_init() is bracketed below.
 *
 * Why the markers live here instead of in the vendored source
 * ----------------------------------------------------------
 * armino/glue/bk_wifi/src/wifi_init.c is byte-identical to
 * the authority copy (cp/components/bk_wifi/src/wifi_init.c), and that identity
 * is evidence this bring-up relies on.  Editing it to add log lines would spend
 * that evidence on a diagnostic we intend to delete.
 *
 * Instead each callee is renamed for the duration of this translation unit
 * only, and a same-signature interposer logs, calls the real function, and logs
 * again.  The vendored file is compiled unmodified; the renames cannot leak
 * because they are #undef'd right after the #include, and every callee is
 * defined in a *different* translation unit (prebuilt archives, wifi_config.c,
 * rw_task.c, rwnx_rx.c), so no definition is renamed by accident.
 *
 * core_thread_init() is deliberately NOT interposed: it is defined in the same
 * TU as its caller (rw_task.c:1161 / :1238), so a rename would hit the
 * definition too.  Its two blocking interior calls are already visible from the
 * OSAL shim markers, which report queue 'core_queue' and thread 'core_thread'
 * by name (hal_port/rtos_compat_shim.c).
 */

#include <bk7258_initseq.h>

/* Real callees, declared before the renames take effect.  Signatures match the
 * vendored declarations: wifi_init.c:55-58 and bk_private/bk_wifi.h:540.
 */

extern void rwnxl_init(void);
extern int calibration_init(void);
extern uint32_t cfg_param_init(void);
extern int rwnx_intf_init(void);
extern int fhost_rxbuf_push(void);
extern void wpas_thread_start(void);
extern void coex_ictw_report_wifi_open_status(bool is_wifi_open);

/* Interposers.  Numbered by the source line they stand in for, so a UART
 * capture maps straight back to wifi_init(): 87, 90, 97, 105, 120, 123, 126.
 */

void initseq_rwnxl_init(void)
{
  BK7258_INITSEQ("87a rwnxl_init enter");
  rwnxl_init();
  BK7258_INITSEQ("87b rwnxl_init leave");
}

int initseq_calibration_init(void)
{
  int ret;

  BK7258_INITSEQ("90a calibration_init enter");
  ret = calibration_init();
  BK7258_INITSEQ_RET("90b calibration_init leave", ret);
  return ret;
}

uint32_t initseq_cfg_param_init(void)
{
  uint32_t ret;

  BK7258_INITSEQ("97a cfg_param_init enter");
  ret = cfg_param_init();
  BK7258_INITSEQ_RET("97b cfg_param_init leave", (int)ret);
  return ret;
}

int initseq_rwnx_intf_init(void)
{
  int ret;

  BK7258_INITSEQ("105a rwnx_intf_init enter");
  ret = rwnx_intf_init();
  BK7258_INITSEQ_RET("105b rwnx_intf_init leave", ret);
  return ret;
}

int initseq_fhost_rxbuf_push(void)
{
  int ret;

  BK7258_INITSEQ("120a fhost_rxbuf_push enter");
  ret = fhost_rxbuf_push();
  BK7258_INITSEQ_RET("120b fhost_rxbuf_push leave", ret);
  return ret;
}

void initseq_wpas_thread_start(void)
{
  BK7258_INITSEQ("123a wpas_thread_start enter");
  wpas_thread_start();
  BK7258_INITSEQ("123b wpas_thread_start leave");
}

void initseq_coex_ictw_report_wifi_open_status(bool is_wifi_open)
{
  BK7258_INITSEQ("126a coex_ictw_report enter");
  coex_ictw_report_wifi_open_status(is_wifi_open);
  BK7258_INITSEQ("126b coex_ictw_report leave");
}

/* Redirect the call sites inside the vendored source.  These are object-like
 * renames, so the `extern void rwnxl_init(void);` style declarations that
 * wifi_init.c repeats at :55-58 stay well-formed after substitution.
 */

#define rwnxl_init                        initseq_rwnxl_init
#define calibration_init                  initseq_calibration_init
#define cfg_param_init                    initseq_cfg_param_init
#define rwnx_intf_init                    initseq_rwnx_intf_init
#define fhost_rxbuf_push                  initseq_fhost_rxbuf_push
#define wpas_thread_start                 initseq_wpas_thread_start
#define coex_ictw_report_wifi_open_status initseq_coex_ictw_report_wifi_open_status

#include "../../armino/glue/bk_wifi/src/wifi_init.c"

#undef rwnxl_init
#undef calibration_init
#undef cfg_param_init
#undef rwnx_intf_init
#undef fhost_rxbuf_push
#undef wpas_thread_start
#undef coex_ictw_report_wifi_open_status

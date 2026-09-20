/* Wi-Fi-only translation unit: establish Armino's global build contract. */
#include <bk_prelude.h>
#include "driver.h"

/* TEMPORARY BRING-UP DIAGNOSTIC -- REMOVE with the rest of the "[initseq]"
 * instrumentation; see hal_port/include/bk7258_initseq.h for the rationale and the
 * full removal recipe.
 *
 * rwnxl_sleep() (rw_task.c:1245) runs synchronously on the caller's thread, so
 * it can hold up bk_wifi_init()'s return.  It is newly reachable this round:
 * its guard is `CONFIG_PM_V2 && CONFIG_STA_PS && (... || CONFIG_SOC_BK7236XX)`
 * and all three now hold (sys_config.h:21,69,70), so this call site is compiled
 * in for the first time.
 *
 * Its own "rwnxl_sleep timeout" print cannot localize the stall: that message is
 * only reached on the timeout branch, so its absence is equally consistent with
 * "never entered" and "completed normally".  The markers below separate those
 * two cases.
 *
 * The body lives in the hash-pinned prebuilt archive (ELF @0209a8bc), i.e. a
 * different translation unit, so renaming the symbol here redirects only the
 * call site and cannot rename a definition.  The vendored rw_task.c is compiled
 * byte-for-byte unmodified.
 */

#include <bk7258_initseq.h>

/* Declared before the rename takes effect.  lmac_msg.h:2184 spells this
 * `extern void rwnxl_sleep();`, which is compatible with this prototype.
 */

extern void rwnxl_sleep(void);

void initseq_rwnxl_sleep(void)
{
  BK7258_INITSEQ("1245a rwnxl_sleep enter");
  rwnxl_sleep();
  BK7258_INITSEQ("1245b rwnxl_sleep leave");
}

#define rwnxl_sleep initseq_rwnxl_sleep

#define CONFIG_AP CONFIG_BK7258_WIFI_AP
#include "../../armino/glue/bk_wifi/src/rw_task.c"
#undef CONFIG_AP

#undef rwnxl_sleep

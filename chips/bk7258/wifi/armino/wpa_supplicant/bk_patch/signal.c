/* NuttX port: the prologue below came from a wrapper translation unit
 * at wifi/hal_port/vendor_sources/wpa_signal.c, which existed only to include
 * this file.  The build lists this file directly now.
 */
/* Wi-Fi-only translation unit: establish Armino's global build contract. */
#include <bk_prelude.h>

/* NuttX has no process-signal shutdown watchdog for the vendor eloop.
 * Reuse the vendor's intentional no-op signal/alarm compatibility source. */

#include <common/bk_include.h>
#include "signal.h"

void bk_signal(int sig_num, SIG_FUNC func)
{
}

extern unsigned int bk_alarm(unsigned int seconds)
{
	return 0;
}

// eof


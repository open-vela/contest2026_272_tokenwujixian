/****************************************************************************
 * board/bk7258-devkit/src/bk7258_boardinit.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/board.h>

#ifdef CONFIG_BK7258_COMPONENT_AP
#  include <arch/chip/bk7258_ap_boot.h>
#endif

void board_early_initialize(void)
{
  /* Early pinmux and console hardware are owned by the secure chip layer. */

#ifdef CONFIG_BK7258_COMPONENT_AP
  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_BOARD_EARLY);
#endif
}

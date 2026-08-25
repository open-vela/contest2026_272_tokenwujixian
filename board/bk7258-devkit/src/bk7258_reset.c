/****************************************************************************
 * board/bk7258-devkit/src/bk7258_reset.c
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_RESET

#include <nuttx/arch.h>
#include <nuttx/board.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reset the local BK7258 Cortex-M core through the architecture-provided
 *   SYSRESETREQ path.  The reset vector then re-enters the normal BK7258
 *   startup trampoline; it is not a CPU1/RPTUN lifecycle operation.
 *
 ****************************************************************************/

int board_reset(int status)
{
  (void)status;

  up_systemreset();
  return 0;
}

#endif /* CONFIG_BOARDCTL_RESET */

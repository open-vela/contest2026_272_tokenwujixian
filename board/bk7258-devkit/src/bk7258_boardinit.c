/****************************************************************************
 * board/bk7258-devkit/src/bk7258_boardinit.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/board.h>

void board_early_initialize(void)
{
  /* Early pinmux and console hardware are owned by the secure chip layer. */
}

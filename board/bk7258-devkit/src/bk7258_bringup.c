/****************************************************************************
 * board/bk7258-devkit/src/bk7258_bringup.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/board.h>

void board_late_initialize(void)
{
  /* UART console registration is chip-owned. Board peripherals are added
   * only after their pinmux and hardware contracts are verified. */
}

int board_app_initialize(uintptr_t arg)
{
  (void)arg;
  return 0;
}

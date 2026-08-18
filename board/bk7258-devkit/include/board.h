/****************************************************************************
 * board/bk7258-devkit/include/board.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BOARD_H
#define __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BOARD_H

#define BOARD_SYSTICK_CLOCK_HZ 32000
#define BOARD_UART0_BAUD   115200
#define BOARD_UART0_TX_PIN 11
#define BOARD_UART0_RX_PIN 10

/* The DevKit schematic routes LED1 through R63 to the red LED3. LED1 is the
 * BK7258 P40/GPIO40 net, and the LED cathode returns to ground, so a high GPIO
 * level turns it on. */

#define BOARD_AP_HEARTBEAT_LED_PIN         40
#define BOARD_AP_HEARTBEAT_LED_ACTIVE_HIGH 1

#endif /* __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BOARD_H */

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

/* Audio Power Amplifier (HT6873) on GPIO50
 *
 * The PA enable pin is active-high.  Safe sequencing:
 *   Enable:  DAC mute 20ms → GPIO50 high → wait 20ms
 *   Disable: DAC mute 10ms → GPIO50 low  → wait 10ms
 */

#define BOARD_AUDIO_PA_PIN                 50
#define BOARD_AUDIO_PA_ACTIVE_HIGH         1
#define BOARD_AUDIO_PA_PRE_ENABLE_MS       20
#define BOARD_AUDIO_PA_POST_ENABLE_MS      20
#define BOARD_AUDIO_PA_PRE_DISABLE_MS      10
#define BOARD_AUDIO_PA_POST_DISABLE_MS     10

/* Buttons (active-low, internal pull-up)
 *
 * KEY1 = GPIO13, KEY2 = GPIO12, KEY3 = GPIO8
 * Each button ID maps to a bit in the NuttX button bitmask.
 */

#define BUTTON_KEY1      0
#define BUTTON_KEY2      1
#define BUTTON_KEY3      2
#define NUM_BUTTONS      3

#define BUTTON_KEY1_BIT  (1u << BUTTON_KEY1)
#define BUTTON_KEY2_BIT  (1u << BUTTON_KEY2)
#define BUTTON_KEY3_BIT  (1u << BUTTON_KEY3)

#define BOARD_KEY1_PIN   13
#define BOARD_KEY2_PIN   12
#define BOARD_KEY3_PIN   8
#define BOARD_KEYS_ACTIVE_LOW 1

/* Round GC9D01 160x160 SPI LCDs on the DevKit display FPC (schematic
 * sheet 5).  The two round panels are driven by the SoC LCD QSPI
 * hardware: LCD0 on QSPI0 (CLK/CS/IO0 = GPIO22/23/24, D/C=GPIO7,
 * RESET=GPIO6) and LCD1 on QSPI1 (CLK/CS/IO0 = GPIO2/3/4, D/C=GPIO5,
 * RESET=GPIO45).  The bit-bang lower-half drives the LCD1/QSPI1 bus
 * through the plain-GPIO path (4-wire SPI, Mode 0).
 *
 * The backlight (LCD_BL_PWM) is GPIO25 per the reference firmware
 * (bk_dual_screen_avi_player: LCD_BL_IO = GPIO25), active-high into the
 * Q3 low-side switch.  GPIO52 enables the LDO_3V3 rail that powers the
 * LCD (reference firmware LDO3V3_CTRL_GPIO=52, active-high); it must be
 * driven high before the panel is powered.
 */

#define BOARD_LCD_SCL_PIN 2
#define BOARD_LCD_CS_PIN  3
#define BOARD_LCD_SDA_PIN 4
#define BOARD_LCD_DC_PIN  5
#define BOARD_LCD_RST_PIN 45
#define BOARD_LCD_BL_PIN  25
#define BOARD_LCD_LDO_PIN 52

#endif /* __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BOARD_H */

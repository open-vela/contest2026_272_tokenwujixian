/****************************************************************************
 * board/bk7258-devkit/src/jd9853.h
 *
 * Private register definitions for the JD9853 (Jadard) SPI TFT LCD driver
 * chip.  Command set is MIPI-DCS compatible plus the vendor password/page
 * scheme used to unlock the chip-specific power, timing and gamma banks.
 *
 * The initialization sequence is the reference sequence used by the
 * open-source ESP-IDF panel driver (mydazy/esp_lcd_jd9853, Apache-2.0) for
 * JD9853-based 240x284/240x296 panels.
 *
 ****************************************************************************/

#ifndef __BOARD_BK7258_DEVKIT_SRC_JD9853_H
#define __BOARD_BK7258_DEVKIT_SRC_JD9853_H

#define JD9853_SLEEP_IN           0x10 /* Sleep in */
#define JD9853_SLEEP_OUT          0x11 /* Sleep out */
#define JD9853_INVERSION_OFF      0x20 /* Display inversion off */
#define JD9853_INVERSION_ON       0x21 /* Display inversion on */
#define JD9853_DISOFF             0x28 /* Display off */
#define JD9853_DISON              0x29 /* Display on */
#define JD9853_CASET              0x2A /* Column address set */
#define JD9853_PASET              0x2B /* Page address set */
#define JD9853_RAMWR              0x2C /* Memory write */
#define JD9853_RAMRD              0x2E /* Memory read */
#define JD9853_TEOFF              0x34 /* Tearing effect line off */
#define JD9853_TEON               0x35 /* Tearing effect line on */
#define JD9853_COLMOD             0x3A /* Pixel format set */
#define JD9853_COLMOD_16BIT       0x55

#define JD9853_MADCTL            0x36 /* Memory data access control */
#define JD9853_MADCTL_MY     (1 << 7) /* Page address order */
#define JD9853_MADCTL_MX     (1 << 6) /* Column address order */
#define JD9853_MADCTL_MV     (1 << 5) /* Page/column order */
#define JD9853_MADCTL_ML     (1 << 4) /* Line address order */
#define JD9853_MADCTL_BGR    (1 << 3) /* Set panel order BGR */

#define JD9853_PASSWORD           0xDF /* Unlock chip-specific registers */
#define JD9853_SET_PAGE_CMD       0xDE /* Select register page */
#define JD9853_PAGE_0             0x00
#define JD9853_PAGE_1             0x01

/* Page 0 */
#define JD9853_SET_DCDC           0xB2 /* DC-DC control */
#define JD9853_GAMMA_SET_CMD      0xB7 /* Gamma setting (page 0) */
#define JD9853_POWER_CTRL         0xBB /* Power control */
#define JD9853_SETSTBA            0xC0 /* Source output driving ability */
#define JD9853_SETPANEL           0xC1 /* Panel related setting */
#define JD9853_SETRGBCYC          0xC3 /* SD output timing */
#define JD9853_SETTCON            0xC4 /* Timing control */
#define JD9853_R_GAMMA_SET_CMD    0xC8 /* Gamma correction */
#define JD9853_SETGD              0xD0 /* Gate function */
#define JD9853_RAM_CTRL           0xD7 /* Interface related setting */
#define JD9853_SETOSCM            0xE6 /* Oscillator M setting */

/* Page 1 */
#define JD9853_SETPANEL_1         0xB7 /* Panel setting (page 1) */
#define JD9853_SETTCON_1          0xC1 /* Timing control (page 1) */
#define JD9853_SETTCON_2          0xC2 /* Timing control (page 1) */
#define JD9853_SETTCON_3          0xC4 /* Timing control (page 1) */
#define JD9853_SETDISP_1          0xBE /* Display setting (page 1) */

FAR struct lcd_dev_s *jd9853_lcdinitialize(FAR struct spi_dev_s *spi);

#endif /* __BOARD_BK7258_DEVKIT_SRC_JD9853_H */

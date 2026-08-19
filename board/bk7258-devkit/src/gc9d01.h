/****************************************************************************
 * board/bk7258-devkit/src/gc9d01.h
 *
 * Private register definitions for the GC9D01 SPI TFT LCD driver chip.
 * Command set is MIPI-DCS compatible.  Used by the DevKit's two round
 * 0.71" 160x160 panels (NT071BB-C12-02).
 *
 ****************************************************************************/

#ifndef __BOARD_BK7258_DEVKIT_SRC_GC9D01_H
#define __BOARD_BK7258_DEVKIT_SRC_GC9D01_H

#define GC9D01_NOP              0x00 /* No operation */
#define GC9D01_SWRESET          0x01 /* Software reset */
#define GC9D01_SLEEP_IN         0x10 /* Sleep in */
#define GC9D01_SLEEP_OUT        0x11 /* Sleep out */
#define GC9D01_INVERSION_OFF    0x20 /* Display inversion off */
#define GC9D01_INVERSION_ON     0x21 /* Display inversion on */
#define GC9D01_DISOFF           0x28 /* Display off */
#define GC9D01_DISON            0x29 /* Display on */
#define GC9D01_CASET            0x2A /* Column address set */
#define GC9D01_PASET            0x2B /* Page address set */
#define GC9D01_RAMWR            0x2C /* Memory write */
#define GC9D01_RAMRD            0x2E /* Memory read */
#define GC9D01_TEOFF            0x34 /* Tearing effect line off */
#define GC9D01_TEON             0x35 /* Tearing effect line on */
#define GC9D01_MADCTL           0x36 /* Memory data access control */
#define GC9D01_COLMOD           0x3A /* Pixel format set */

#define GC9D01_MADCTL_MY     (1 << 7) /* Page address order */
#define GC9D01_MADCTL_MX     (1 << 6) /* Column address order */
#define GC9D01_MADCTL_MV     (1 << 5) /* Page/column order */
#define GC9D01_MADCTL_ML     (1 << 4) /* Line address order */
#define GC9D01_MADCTL_BGR    (1 << 3) /* Set panel order BGR */

FAR struct lcd_dev_s *gc9d01_lcdinitialize(FAR struct spi_dev_s *spi);

#endif /* __BOARD_BK7258_DEVKIT_SRC_GC9D01_H */

#ifndef __VENDOR_BEKEN_CHIP_BK7258_QSPI_H
#define __VENDOR_BEKEN_CHIP_BK7258_QSPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/config.h>
#include <nuttx/spi/qspi.h>

/* Pins the LCD FPC uses.  Only CLK, CSN and IO0 are routed to the QSPI1
 * controller (peripheral function 6); D/C stays a plain GPIO output, which is
 * what the authority does for its 4-line serial panels on this controller
 * (middleware/driver/lcd/lcd_spi_driver.c: gpio_dev_unmap(dc_pin) + output).
 */

#define BK7258_LCD_QSPI_CLK_PIN   2      /* QSPI1_CLK, was SPI1_SCK  */
#define BK7258_LCD_QSPI_CS_PIN    3      /* QSPI1_CSN, was SPI1_CSN  */
#define BK7258_LCD_QSPI_IO0_PIN   4      /* QSPI1_IO0, was SPI1_MOSI */
#define BK7258_LCD_QSPI_DC_PIN    5      /* plain GPIO, was SPI1 D/C */

/* Bring QSPI1 up as a NuttX qspi_dev_s and program its clock.  Returns NULL
 * when the peripheral is not available.
 */

FAR struct qspi_dev_s *bk7258_qspi_initialize(uint32_t frequency);

/* Panel command channel.  Write-only: the DevKit display connector carries no
 * return line, so the panel cannot be read back (jd9853.c:723).  Register reads
 * are therefore absent by design, not by omission.
 */

int bk7258_qspi_write_cmd(uint8_t cmd);
int bk7258_qspi_write_data(FAR const void *data, size_t len);
int bk7258_qspi_write_cmd_data(uint8_t cmd, FAR const void *data, size_t len);

/* Pixel burst.  begin/write/end bracket a frame; the controller asserts chip
 * select per command frame, so a frame of rows is a plain run of frames with
 * D/C held high.
 */

int bk7258_qspi_pixels_begin(void);
int bk7258_qspi_pixels_write(FAR const void *data, size_t len);
int bk7258_qspi_pixels_end(void);
int bk7258_qspi_write_pixels(FAR const void *data, size_t len);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_QSPI_H */

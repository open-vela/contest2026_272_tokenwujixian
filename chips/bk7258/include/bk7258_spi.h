/****************************************************************************
 * chips/bk7258/include/bk7258_spi.h
 *
 * Register layout and low-level helpers for the BK7258 SPI1 controller.
 *
 * The register structure below is derived from the Beken ARMINO SDK file
 * middleware/soc/bk7258/soc/spi_struct.h (release/v1.6.0) and retains its
 * original copyright and Apache-2.0 license.
 *
 * Copyright 2020-2021 Beken
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_SPI_H
#define __VENDOR_BEKEN_CHIP_BK7258_SPI_H

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SPI1 is the controller behind the DevKit display FPC "QSPI1" net.  Its
 * four pads (CLK/CS/MOSI/MISO = GPIO2/3/4/5) are the SPI1 peripheral mux
 * option 0.  The base is SOC_SPI_REG_BASE + 0x1010000 * unit.
 */

#define BK7258_SPI0_BASE       UINT32_C(0x44870000)
#define BK7258_SPI1_BASE       UINT32_C(0x45880000)
#define BK7258_SPI1_STRIDE     UINT32_C(0x01010000)

/* System control registers shared with the CP bring-up.  The device clock
 * gate (SYS_BASE+0x30) already exists as BK7258_SYS_DEV_CLK_EN; spi1_cken
 * is bit 9.  The SPI clock source select register (SYS_BASE+0x28) holds
 * clksel_spi1 at bit 5 (0 = XTAL 26M, 1 = APLL).
 */

#define BK7258_SYS_BASE       UINT32_C(0x44010000)
#define BK7258_SYS_CLKSEL     (BK7258_SYS_BASE + UINT32_C(0x28))
#define BK7258_SYS_SPI1_XTAL   UINT32_C(0)          /* clksel_spi1 = 0 */
#define BK7258_SYS_SPI1_APLL   (UINT32_C(1) << 5)   /* clksel_spi1 = 1 */
#define BK7258_SYS_SPI1_CLK_EN (UINT32_C(1) << 9)   /* spi1_cken in DEV_CLK_EN */

/* SPI1 source clock (XTAL path) and the resulting maximum SPI baud rate.
 * baud = f_src / (2 * clk_rate), so with clk_rate = 1 the top rate on the
 * XTAL source is 13 MHz.
 */

#define BK7258_SPI1_SRC_CLK    UINT32_C(26000000)
#define BK7258_SPI1_MAX_BAUD   (BK7258_SPI1_SRC_CLK / 2)

/* The tx_trans_len field is 12 bits, so a single master transfer is capped
 * at 4095 bytes.  Larger bursts are split by the driver while software CS
 * stays asserted.
 */

#define BK7258_SPI_MAX_TRANSFER 4095

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* SPI1 register block.  Fields match the ARMINO spi_struct.h layout. */

typedef volatile struct
{
  /* REG_0x00 */
  uint32_t dev_id;

  /* REG_0x01 */
  uint32_t dev_version;

  /* REG_0x02 */
  union
  {
    struct
    {
      uint32_t soft_reset:      1;  /* bit[0] SPI soft reset */
      uint32_t clk_gate_bypass: 1;  /* bit[1] bypass SPI clock gate */
      uint32_t reserved:       30;  /* bit[2:31] */
    };
    uint32_t v;
  } global_ctrl;

  /* REG_0x03 */
  uint32_t dev_status;

  /* REG_0x04 */
  union
  {
    struct
    {
      uint32_t tx_fifo_int_level:    2;  /* bit[0:1] */
      uint32_t rx_fifo_int_level:    2;  /* bit[2:3] */
      uint32_t tx_udf_int_en:        1;  /* bit[4] tx fifo underflow int enable */
      uint32_t rx_ovf_int_en:        1;  /* bit[5] rx fifo overflow int enable */
      uint32_t tx_fifo_int_en:       1;  /* bit[6] */
      uint32_t rx_fifo_int_en:       1;  /* bit[7] */
      uint32_t clk_rate:             8;  /* bit[8:15] SPI clock rate divider */
      uint32_t slave_release_int_en: 1;  /* bit[16] */
      uint32_t wire3_en:             1;  /* bit[17] 0: 4 wire, 1: 3 wire */
      uint32_t bit_width:            1;  /* bit[18] 0: 8bit, 1: 16bit */
      uint32_t lsb_first_en:         1;  /* bit[19] 0: MSB first, 1: LSB first */
      uint32_t cpol:                 1;  /* bit[20] */
      uint32_t cpha:                 1;  /* bit[21] */
      uint32_t master_en:            1;  /* bit[22] */
      uint32_t enable:               1;  /* bit[23] */
      uint32_t byte_interval:        6;  /* bit[24:29] */
      uint32_t reserved:             2;  /* bit[30:31] */
    };
    uint32_t v;
  } ctrl;

  /* REG_0x05 */
  union
  {
    struct
    {
      uint32_t tx_en:            1;  /* bit[0] */
      uint32_t rx_en:            1;  /* bit[1] */
      uint32_t tx_finish_int_en: 1;  /* bit[2] */
      uint32_t rx_finish_int_en: 1;  /* bit[3] */
      uint32_t reserved:         4;  /* bit[4:7] */
      uint32_t tx_trans_len:    12;  /* bit[8:19] */
      uint32_t rx_trans_len:    12;  /* bit[20:31] */
    };
    uint32_t v;
  } cfg;

  /* REG_0x06 */
  union
  {
    struct
    {
      uint32_t reserved0:         1;  /* bit[0] */
      uint32_t tx_fifo_wr_ready:  1;  /* bit[1] */
      uint32_t rx_fifo_rd_ready:  1;  /* bit[2] */
      uint32_t reserved1:         1;  /* bit[3] */
      uint32_t reserved2:         4;  /* bit[4:7] */
      uint32_t tx_fifo_int:       1;  /* bit[8] */
      uint32_t rx_fifo_int:       1;  /* bit[9] */
      uint32_t slave_release_int: 1;  /* bit[10] */
      uint32_t tx_underflow_int:  1;  /* bit[11] */
      uint32_t rx_overflow_int:   1;  /* bit[12] */
      uint32_t tx_finish_int:     1;  /* bit[13] */
      uint32_t rx_finish_int:     1;  /* bit[14] */
      uint32_t reserved3:         1;  /* bit[15] */
      uint32_t tx_fifo_clr:       1;  /* bit[16] */
      uint32_t rx_fifo_clr:       1;  /* bit[17] */
      uint32_t reserved4:        14;  /* bit[18:31] */
    };
    uint32_t v;
  } int_status;

  /* REG_0x07 */
  union
  {
    struct
    {
      uint32_t fifo_data: 16;        /* bit[0:15] TX/RX data */
      uint32_t reserved:  16;        /* bit[16:31] */
    };
    uint32_t v;
  } data;
} bk7258_spi_hw_t;

#endif /* __VENDOR_BEKEN_CHIP_BK7258_SPI_H */

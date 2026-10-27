/****************************************************************************
 * chips/bk7258/include/driver/psram_types.h
 *
 * This file replaces cp/include/driver/psram_types.h on the include path once
 * the preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
 * shows the two declare the same contract.  The implementation is
 * chips/bk7258/bk7258_psram.c.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_PSRAM_TYPES_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_PSRAM_TYPES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* easyflash key holding the detected chip id */

#define PSRAM_CHIP_ID "psram_id"

#define BK_ERR_PSRAM_SERVER_NOT_INIT   (BK_ERR_PSRAM_BASE - 1)
#define BK_ERR_PSRAM_ADDR_ALIGN        (BK_ERR_PSRAM_BASE - 2)
#define BK_ERR_PSRAM_ADDR_OUT_OF_RANGE (BK_ERR_PSRAM_BASE - 3)
#define BK_ERR_PSRAM_ADDR_RELATION     (BK_ERR_PSRAM_BASE - 4)
#define BK_ERR_PSRAM_AREA              (BK_ERR_PSRAM_BASE - 5)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Controller clock: F/(2 + (1 + div)), with a 320 MHz or 480 MHz source. */

typedef enum
{
  PSRAM_240M = 0,
  PSRAM_160M,
  PSRAM_120M,
  PSRAM_80M
} psram_clk_t;

/* The two 3.x V entries only mean something on the BK7256; on this SoC
 * PSRAM_OUT_1_95V is what the driver selects.
 */

typedef enum
{
  PSRAM_OUT_3_20V = 0,
  PSRAM_OUT_3_0V,
  PSRAM_OUT_2_0V,
  PSRAM_OUT_1_95V,
  PSRAM_OUT_1_90V,
  PSRAM_OUT_1_85V,
  PSRAM_OUT_1_80V,
} psram_voltage_t;

typedef enum
{
  PSRAM_INCRASE_TRANS = 0,
  PSRAM_32_WORD_WRAP,
  PSRAM_64_WORD_WRAP,
  PSRAM_128_WORD_WRAP,
  PSRAM_256_WORD_WRAP,
  PSRAM_512_WORD_WRAP,
  PSRAM_1024_WORD_WRAP,
  PSRAM_2048_WORD_WRAP
} psram_tansfer_mode_t;

typedef enum
{
  PSRAM_WRITE_THROUGH_AREA_0 = 0,
  PSRAM_WRITE_THROUGH_AREA_1,
  PSRAM_WRITE_THROUGH_AREA_2,
  PSRAM_WRITE_THROUGH_AREA_3,
  PSRAM_WRITE_THROUGH_AREA_COUNT
} psram_write_through_area_t;

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_PSRAM_TYPES_H */

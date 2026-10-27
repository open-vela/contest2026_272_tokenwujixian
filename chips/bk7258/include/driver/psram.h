/****************************************************************************
 * chips/bk7258/include/driver/psram.h
 *
 * This file replaces cp/include/driver/psram.h on the include path once the
 * preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py)
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_PSRAM_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_PSRAM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <driver/psram_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Byte, half-word and word writes through a 32-bit port: the PSRAM window
 * only takes aligned word accesses, so the authority does the
 * read-modify-write here.  These are the authority's bodies.
 */

static inline void bk_psram_byte_write(unsigned char *addr,
                                       unsigned char value)
{
  uint32_t addr_align = (uint32_t)addr;
  uint32_t temp = 0;
  uint8_t index = 0;

  index = (addr_align & 0x3);
  addr_align &= 0xFFFFFFFC;
  temp = *((volatile uint32_t *)addr_align);

  switch (index)
    {
      case 0:
        temp &= ~(0xFF);
        temp |= value;
        break;

      case 1:
        temp &= ~(0xFF << 8);
        temp |= (value << 8);
        break;

      case 2:
        temp &= ~(0xFF << 16);
        temp |= (value << 16);
        break;

      case 3:
        temp &= ~(0xFF << 24);
        temp |= (value << 24);

      default:
        break;
    }

  *((volatile uint32_t *)addr_align) = temp;
}

static inline void bk_psram_half_word_write(unsigned short *dst,
                                            unsigned short value)
{
  int index = (unsigned int)dst & 0x03;
  unsigned int *dst_t = (unsigned int *)((unsigned int)dst & 0xFFFFFFFC);
  unsigned int load[2] = {0};
  unsigned short *p;

  if (index < 3)
    {
      load[0] = *dst_t;
      p = (unsigned short *)((unsigned char *)&load[0] + index);
      *p = value;
      *dst_t = load[0];
    }
  else
    {
      load[0] = *dst_t;
      load[1] = *(dst_t + 1);
      p = (unsigned short *)((unsigned char *)&load[0] + index);
      *p = value;
      *dst_t = load[0];
      *(dst_t + 1) = load[1];
    }
}

static inline void bk_psram_word_write(unsigned int *src, unsigned int value)
{
  int index = (unsigned int)src & 0x03;
  unsigned int *dst = (unsigned int *)((unsigned int)src & 0xFFFFFFFC);

  if (!index)
    {
      *dst = value;
    }
  else
    {
      unsigned int load[2] = {0};
      unsigned int *p_dst =
        (unsigned int *)(((unsigned char *)&load[0]) + index);

      load[0] = *dst;
      load[1] = *(dst + 1);
      *p_dst = value;
      *dst = load[0];
      *(dst + 1) = load[1];
    }
}

static inline void bk_psram_word_memcpy(void *dst_t, void *src_t,
                                        unsigned int length)
{
  unsigned int *dst = (unsigned int *)dst_t, *src = (unsigned int *)src_t;
  int index = (unsigned int)dst & 0x03;
  int count = length >> 2, tail = length & 0x3;
  unsigned int tmp = 0;
  unsigned char *p, *pp = (unsigned char *)src;

  if (!index)
    {
      while (count--)
        {
          *dst++ = pp[0] | pp[1] << 8 | pp[2] << 16 | pp[3] << 24;
          pp += 4;
        }

      if (tail)
        {
          tmp = *dst;
          p = (unsigned char *)&tmp;
          while (tail--)
            {
              *p++ = *pp++;
            }

          *dst = tmp;
        }
    }
  else
    {
      unsigned int *pre_dst =
        (unsigned int *)((unsigned int)dst & 0xFFFFFFFC);
      unsigned int pre_count = 4 - index;

      tmp = *pre_dst;
      p = (unsigned char *)&tmp + index;
      if (pre_count > length)
        {
          pre_count = length;
        }

      while (pre_count--)
        {
          *p++ = *pp++;
          length--;
        }

      *pre_dst = tmp;

      if (length <= 0)
        {
          return;
        }

      dst = pre_dst + 1;
      count = length >> 2;
      tail = length & 0x3;
      while (count--)
        {
          *dst++ = pp[0] | pp[1] << 8 | pp[2] << 16 | pp[3] << 24;
          pp += 4;
        }

      if (tail)
        {
          tmp = *dst;
          p = (unsigned char *)&tmp;
          while (tail--)
            {
              *p++ = *pp++;
            }

          *dst = tmp;
        }
    }
}

/* The empty parameter list on bk_psram_heap_init_flag_get is the
 * authority's declaration, kept as written.
 */

bool bk_psram_heap_init_flag_get();
bk_err_t bk_psram_heap_init_flag_set(bool init);
bk_err_t bk_psram_id_auto_detect(void);
bk_err_t bk_psram_init(void);
bk_err_t bk_psram_deinit(void);

bk_err_t bk_psram_memcpy(uint8_t *start_addr, uint8_t *data_buf,
                         uint32_t len);
bk_err_t bk_psram_memread(uint8_t *start_addr, uint8_t *data_buf,
                          uint32_t len);

bk_err_t bk_psram_set_clk(psram_clk_t clk);
bk_err_t bk_psram_set_voltage(psram_voltage_t voltage);
char *bk_psram_strcat(char *start_addr, const char *data_buf);

psram_write_through_area_t bk_psram_alloc_write_through_channel(void);
bk_err_t bk_psram_free_write_through_channel(psram_write_through_area_t area);
bk_err_t bk_psram_enable_write_through(psram_write_through_area_t area,
                                       uint32_t start, uint32_t end);
bk_err_t bk_psram_disable_write_through(psram_write_through_area_t area);
bk_err_t bk_psram_calibrate(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_PSRAM_H */

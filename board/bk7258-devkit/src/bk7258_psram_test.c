/****************************************************************************
 * board/bk7258-devkit/src/bk7258_psram_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 PSRAM large-allocation integrity test.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <arch/chip/bk7258_memorymap.h>

#define BK7258_PSRAM_TEST_DEFAULT_KIB  8192ul
#define BK7258_PSRAM_TEST_MIN_KIB      64ul

static void bk7258_psram_test_usage(const char *program)
{
  printf("Usage: %s [KiB]\n", program);
  printf("  Allocate, write, verify and free a PSRAM heap block.\n");
  printf("  Default: %lu KiB; range: %lu..%lu KiB.\n",
         BK7258_PSRAM_TEST_DEFAULT_KIB, BK7258_PSRAM_TEST_MIN_KIB,
         (unsigned long)(BK7258_PSRAM_WINDOW_SIZE / 1024));
}

static int bk7258_psram_test_parse_size(const char *text, size_t *size)
{
  char *end;
  unsigned long kib;

  errno = 0;
  kib = strtoul(text, &end, 0);
  if (errno != 0 || *text == '\0' || *end != '\0' ||
      kib < BK7258_PSRAM_TEST_MIN_KIB ||
      kib > BK7258_PSRAM_WINDOW_SIZE / 1024)
    {
      return -EINVAL;
    }

  *size = kib * 1024;
  return OK;
}

static bool bk7258_psram_test_is_mapped(const void *memory, size_t size)
{
  uintptr_t start = (uintptr_t)memory;
  uintptr_t end = start + size;

  return end >= start && start >= BK7258_PSRAM_BASE &&
         end <= BK7258_PSRAM_BASE + BK7258_PSRAM_WINDOW_SIZE;
}

static int bk7258_psram_test_pattern(volatile uint32_t *memory, size_t words,
                                      uint32_t seed)
{
  size_t index;

  for (index = 0; index < words; index++)
    {
      memory[index] = seed ^ (uint32_t)index;
    }

  __asm__ volatile ("dsb" : : : "memory");

  for (index = 0; index < words; index++)
    {
      uint32_t expected = seed ^ (uint32_t)index;
      uint32_t actual = memory[index];

      if (actual != expected)
        {
          printf("[bk7258_psram_test] mismatch offset=%lu expected=0x%08" PRIx32
                 " actual=0x%08" PRIx32 "\n",
                 (unsigned long)(index * sizeof(*memory)), expected, actual);
          return -EIO;
        }
    }

  return OK;
}

int main(int argc, char *argv[])
{
  static const uint32_t patterns[] =
    {
      UINT32_C(0x00000000), UINT32_C(0xffffffff), UINT32_C(0x5aa55aa5)
    };
  volatile uint32_t *memory;
  size_t size = BK7258_PSRAM_TEST_DEFAULT_KIB * 1024;
  size_t words;
  size_t index;
  int ret;

  if (argc == 2 && (argv[1][0] == '-' || argv[1][0] == '?'))
    {
      bk7258_psram_test_usage(argv[0]);
      return OK;
    }

  if (argc > 2 || (argc == 2 &&
      bk7258_psram_test_parse_size(argv[1], &size) < 0))
    {
      bk7258_psram_test_usage(argv[0]);
      return -EINVAL;
    }

  printf("[bk7258_psram_test] allocating %lu KiB\n",
         (unsigned long)(size / 1024));
  memory = malloc(size);
  if (memory == NULL)
    {
      printf("[bk7258_psram_test] malloc failed for %lu KiB\n",
             (unsigned long)(size / 1024));
      return -ENOMEM;
    }

  if (!bk7258_psram_test_is_mapped((const void *)memory, size))
    {
      printf("[bk7258_psram_test] FAIL: allocation %p (%lu KiB) is outside "
             "PSRAM [0x%08" PRIx32 ", 0x%08" PRIx32 ")\n",
             (void *)memory, (unsigned long)(size / 1024), BK7258_PSRAM_BASE,
             BK7258_PSRAM_BASE + BK7258_PSRAM_WINDOW_SIZE);
      free((void *)memory);
      return -ENXIO;
    }

  printf("[bk7258_psram_test] PSRAM allocation %p size=%lu KiB\n",
         (void *)memory, (unsigned long)(size / 1024));
  words = size / sizeof(*memory);

  for (index = 0; index < sizeof(patterns) / sizeof(patterns[0]); index++)
    {
      ret = bk7258_psram_test_pattern(memory, words, patterns[index]);
      if (ret < 0)
        {
          free((void *)memory);
          return ret;
        }
    }

  free((void *)memory);
  printf("[bk7258_psram_test] PASS: %lu KiB, %lu patterns\n",
         (unsigned long)(size / 1024),
         (unsigned long)(sizeof(patterns) / sizeof(patterns[0])));
  return OK;
}

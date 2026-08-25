/****************************************************************************
 * contest2026_272_tokenwujixian/app/bk7258_gdma_test/bk7258_gdma_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <arch/chip/bk7258_dma.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GDMA_TEST_WORDS       128
#define GDMA_TEST_GUARD_BYTES 16
#define GDMA_TEST_TIMEOUT_SEC 2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gdma_test_destination_s
{
  uint8_t left_guard[GDMA_TEST_GUARD_BYTES];
  uint32_t payload[GDMA_TEST_WORDS];
  uint8_t right_guard[GDMA_TEST_GUARD_BYTES];
};

struct gdma_test_callback_s
{
  sem_t complete;
  volatile unsigned int count;
  volatile int result;
};

static uint32_t g_source[GDMA_TEST_WORDS] __attribute__((aligned(4)));
static uint32_t g_expected[GDMA_TEST_WORDS] __attribute__((aligned(4)));
static struct gdma_test_destination_s g_destination
  __attribute__((aligned(4)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool gdma_test_guards_valid(void)
{
  unsigned int index;

  for (index = 0; index < GDMA_TEST_GUARD_BYTES; index++)
    {
      if (g_destination.left_guard[index] != 0x5a ||
          g_destination.right_guard[index] != 0x3c)
        {
          return false;
        }
    }

  return true;
}

static void gdma_test_callback(BK7258_DMA_HANDLE handle, int result,
                               FAR void *arg)
{
  FAR struct gdma_test_callback_s *state =
    (FAR struct gdma_test_callback_s *)arg;

  (void)handle;
  state->result = result;
  state->count++;
  sem_post(&state->complete);
}

static int gdma_test_wait(FAR struct gdma_test_callback_s *state)
{
  struct timespec deadline;

  if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0)
    {
      return -errno;
    }

  deadline.tv_sec += GDMA_TEST_TIMEOUT_SEC;
  if (sem_clockwait(&state->complete, CLOCK_MONOTONIC, &deadline) < 0)
    {
      return -errno;
    }

  return state->result;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct bk7258_dma_config_s config;
  struct bk7258_dma_config_s invalid;
  struct bk7258_dma_diagnostics_s diagnostics;
  struct gdma_test_callback_s state;
  BK7258_DMA_HANDLE channel;
  BK7258_DMA_HANDLE stale;
  unsigned int index;
  int ret;

  (void)argc;
  (void)argv;

  for (index = 0; index < GDMA_TEST_WORDS; index++)
    {
      g_source[index] = UINT32_C(0x1a2b0000) ^
                        (index * UINT32_C(0x01010101));
      g_expected[index] = g_source[index];
      g_destination.payload[index] = UINT32_C(0xa5a5a5a5);
    }

  memset(g_destination.left_guard, 0x5a, sizeof(g_destination.left_guard));
  memset(g_destination.right_guard, 0x3c,
         sizeof(g_destination.right_guard));
  memset(&state, 0, sizeof(state));
  state.result = -EINPROGRESS;

  if (sem_init(&state.complete, 0, 0) < 0)
    {
      printf("[bk7258_gdma_test] FAIL sem_init=%d\n", errno);
      return 1;
    }

  channel = bk7258_dmachannel();
  if (channel == 0)
    {
      printf("[bk7258_gdma_test] FAIL no GDMA0 channel\n");
      sem_destroy(&state.complete);
      return 1;
    }

  stale = channel;
  if (bk7258_dmafree(channel) != OK || bk7258_dmafree(stale) != -EINVAL)
    {
      printf("[bk7258_gdma_test] FAIL release validation\n");
      sem_destroy(&state.complete);
      return 1;
    }

  channel = bk7258_dmachannel();
  if (channel == 0)
    {
      printf("[bk7258_gdma_test] FAIL channel reallocation\n");
      sem_destroy(&state.complete);
      return 1;
    }

  memset(&config, 0, sizeof(config));
  config.src_addr = (uintptr_t)g_source;
  config.dst_addr = (uintptr_t)g_destination.payload;
  config.nbytes = sizeof(g_source);
  config.src_request = BK7258_DMA_REQ_MEMORY;
  config.dst_request = BK7258_DMA_REQ_MEMORY;
  config.src_width = BK7258_DMA_WIDTH_32BIT;
  config.dst_width = BK7258_DMA_WIDTH_32BIT;
  config.src_increment = true;
  config.dst_increment = true;

  invalid = config;
  invalid.nbytes--;
  ret = bk7258_dmasetup(stale, &config);
  if (ret != -EINVAL)
    {
      printf("[bk7258_gdma_test] FAIL stale handle ret=%d\n", ret);
      bk7258_dmafree(channel);
      sem_destroy(&state.complete);
      return 1;
    }

  ret = bk7258_dmasetup(channel, &invalid);
  if (ret != -EINVAL)
    {
      printf("[bk7258_gdma_test] FAIL invalid config ret=%d\n", ret);
      bk7258_dmafree(channel);
      sem_destroy(&state.complete);
      return 1;
    }

  ret = bk7258_dmasetup(channel, &config);
  if (ret == OK)
    {
      ret = bk7258_dmastart(channel, gdma_test_callback, &state);
    }

  if (ret == OK)
    {
      printf("[bk7258_gdma_test] start handle=%lu src=%p dst=%p bytes=%lu\n",
             (unsigned long)channel, (FAR void *)config.src_addr,
             (FAR void *)config.dst_addr, (unsigned long)config.nbytes);
      ret = gdma_test_wait(&state);
    }

  if (ret != OK || state.count != 1 ||
      memcmp(g_source, g_expected, sizeof(g_source)) != 0 ||
      memcmp(g_destination.payload, g_expected, sizeof(g_expected)) != 0 ||
      !gdma_test_guards_valid())
    {
      printf("[bk7258_gdma_test] FAIL ret=%d callbacks=%u\n", ret,
             state.count);
      if (bk7258_dma_get_diagnostics(channel, &diagnostics) == OK)
        {
          printf("[bk7258_gdma_test] diag pri=%08lx ctl=%08lx req=%08lx "
                 "stat=%08lx gate=%08lx\n",
                 (unsigned long)diagnostics.prio_mode,
                 (unsigned long)diagnostics.control,
                 (unsigned long)diagnostics.req_mux,
                 (unsigned long)diagnostics.status,
                 (unsigned long)diagnostics.cpu0_int_enable);
          printf("[bk7258_gdma_test] diag src=%08lx dst=%08lx irq=%lu "
                 "events=%08lx running=%d\n",
                 (unsigned long)diagnostics.src_addr,
                 (unsigned long)diagnostics.dst_addr,
                 (unsigned long)diagnostics.irq_count,
                 (unsigned long)diagnostics.last_events,
                 diagnostics.running);
          printf("[bk7258_gdma_test] diag dloop=%08lx/%08lx "
                 "sloop=%08lx/%08lx\n",
                 (unsigned long)diagnostics.dst_loop_start,
                 (unsigned long)diagnostics.dst_loop_end,
                 (unsigned long)diagnostics.src_loop_start,
                 (unsigned long)diagnostics.src_loop_end);
          printf("[bk7258_gdma_test] diag secure=%08lx privileged=%08lx\n",
                 (unsigned long)diagnostics.secure_attr,
                 (unsigned long)diagnostics.privileged_attr);
        }

      bk7258_dmastop(channel);
      bk7258_dmafree(channel);
      sem_destroy(&state.complete);
      return 1;
    }

  printf("[bk7258_gdma_test] PASS channel=%lu bytes=%lu "
         "callbacks=%u\n", (unsigned long)((uintptr_t)channel),
         (unsigned long)sizeof(g_source), state.count);
  bk7258_dmafree(channel);
  sem_destroy(&state.complete);
  return 0;
}

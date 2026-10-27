/****************************************************************************
 * app/bk7258_platform_test/drvtest.c
 *
 * "bk7258_drvtest" -- probe the native driver faces from the CP shell.
 *
 *   bk7258_drvtest uart7            exercise the seven live UART symbols
 *   bk7258_drvtest trap             exception lock / repair / direct UART
 *   bk7258_drvtest model            device-model table round trip
 *   bk7258_drvtest rtc              /dev/rtc0 and CLOCK_REALTIME
 *   bk7258_drvtest wdg              /dev/watchdog0 status/keepalive
 *   bk7258_drvtest gpio             /dev/gpio0 write + read back
 *   bk7258_drvtest wdt peek         AON PMU R0 / R7A and the cached reason
 *   bk7258_drvtest wdt stop         disarm the interrupt watchdog
 *   bk7258_drvtest wdt start <ms>   arm it (0 = the 8 s default)
 *   bk7258_drvtest wdt feed         feed once
 *   bk7258_drvtest wdt reason <n>   record a reset reason in both domains
 *   bk7258_drvtest wdt sensor       sample the ADC and read the value store
 *   bk7258_drvtest wdt reset        force reboot; never returns
 *
 * Test-only: CONFIG_LVX_USE_DEMO_CONTEST2026_272_DRVTEST, default n.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>

#include <arch/chip/bk7258_flash.h>

#include "bk7258_probe.h"
#include "bk7258_reset.h"

static void usage(void)
{
  printf("usage: bk7258_drvtest uart7\n"
         "       bk7258_drvtest trap\n"
         "       bk7258_drvtest model\n"
         "       bk7258_drvtest rtc\n"
         "       bk7258_drvtest wdg\n"
         "       bk7258_drvtest gpio\n"
         "       bk7258_drvtest mtd\n"
         "       bk7258_drvtest wdt peek|stop|feed|sensor|reset\n"
         "       bk7258_drvtest wdt start <ms>\n"
         "       bk7258_drvtest wdt reason <n>\n");
}

static int wdt_command(int argc, char *argv[])
{
  if (argc < 3)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[2], "peek") == 0)
    {
      bk7258_wdt_test_peek();
    }
  else if (strcmp(argv[2], "stop") == 0)
    {
      printf("bk7258_drvtest: bk_wdt_stop -> %d\n", bk7258_wdt_test_stop());
      printf("bk7258_drvtest: interrupt watchdog disarmed; the board must stay "
             "up now\n");
    }
  else if (strcmp(argv[2], "start") == 0)
    {
      unsigned long ms = argc > 3 ? strtoul(argv[3], NULL, 10) : 0;

      printf("bk7258_drvtest: bk_wdt_start(%lu) -> %d\n", ms,
             bk7258_wdt_test_start((unsigned int)ms));
    }
  else if (strcmp(argv[2], "feed") == 0)
    {
      printf("bk7258_drvtest: bk_wdt_feed -> %d\n", bk7258_wdt_test_feed());
    }
  else if (strcmp(argv[2], "reason") == 0)
    {
      unsigned long reason = argc > 3 ? strtoul(argv[3], NULL, 0) : 0;

      printf("bk7258_drvtest: reason(0x%lx) -> %d\n", reason,
             bk7258_wdt_test_setreason((unsigned int)reason));
      bk7258_wdt_test_peek();
    }
  else if (strcmp(argv[2], "sensor") == 0)
    {
      printf("bk7258_drvtest: sensor -> %d\n", bk7258_wdt_test_sensor());
    }
  else if (strcmp(argv[2], "reset") == 0)
    {
      printf("bk7258_drvtest: bk_wdt_force_reboot()\n");
      fflush(stdout);
      bk7258_wdt_test_force_reset();
    }
  else
    {
      usage();
      return 1;
    }

  return 0;
}

/* /dev/mtd0 is the flash block's standard face.  This NuttX has no MTD
 * character device, so the node is reached the way a filesystem reaches it:
 * find_mtddriver() gives the inode and inode->u.i_mtd the mtd_dev_s.
 * Compare that face with the Armino one over the same bytes: if they
 * disagree, the block has two implementations again.
 */

#define BK7258_DRVTEST_MTD_SAMPLES 32

static int mtd_command(void)
{
  struct mtd_geometry_s geo;
  struct inode *node;
  struct mtd_dev_s *mtd;
  unsigned char via_mtd[BK7258_DRVTEST_MTD_SAMPLES];
  unsigned char via_armino[BK7258_DRVTEST_MTD_SAMPLES];
  ssize_t nread;
  int ret;

  ret = find_mtddriver("/dev/mtd0", &node);
  if (ret < 0)
    {
      printf("mtd: find_mtddriver(/dev/mtd0) failed: %d\n", ret);
      return 1;
    }

  mtd = node->u.i_mtd;
  if (mtd == NULL)
    {
      printf("mtd: /dev/mtd0 has no mtd_dev_s\n");
      close_mtddriver(node);
      return 1;
    }

  ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY, (unsigned long)&geo);
  if (ret < 0)
    {
      printf("mtd: MTDIOC_GEOMETRY failed: %d\n", ret);
      close_mtddriver(node);
      return 1;
    }

  printf("mtd: blocksize=%lu erasesize=%lu neraseblocks=%lu model=%s\n",
         (unsigned long)geo.blocksize, (unsigned long)geo.erasesize,
         (unsigned long)geo.neraseblocks, geo.model);

  nread = MTD_READ(mtd, 0, sizeof(via_mtd), via_mtd);
  close_mtddriver(node);
  if (nread != (ssize_t)sizeof(via_mtd))
    {
      printf("mtd: MTD_READ returned %ld\n", (long)nread);
      return 1;
    }

  if (bk7258_flash_initialize() < 0 ||
      bk7258_flash_read(0, via_armino, sizeof(via_armino)) < 0)
    {
      printf("mtd: Armino face read failed\n");
      return 1;
    }

  if (memcmp(via_mtd, via_armino, sizeof(via_mtd)) != 0)
    {
      printf("mtd: MISMATCH between the mtd face and bk_flash_read\n");
      return 1;
    }

  printf("mtd: first %u bytes identical on both faces\n",
         (unsigned)BK7258_DRVTEST_MTD_SAMPLES);
  return 0;
}

int main(int argc, char *argv[])
{
  if (argc < 2)
    {
      usage();
      return 1;
    }

  if (strcmp(argv[1], "uart7") == 0)
    {
      bk7258_uart_probe();
      return 0;
    }

  if (strcmp(argv[1], "trap") == 0)
    {
      bk7258_trap_probe();
      return 0;
    }

  if (strcmp(argv[1], "model") == 0)
    {
      bk7258_drv_model_probe();
      return 0;
    }

  if (strcmp(argv[1], "rtc") == 0)
    {
      bk7258_rtc_probe();
      return 0;
    }

  if (strcmp(argv[1], "wdg") == 0)
    {
      bk7258_wdg_probe();
      return 0;
    }

  if (strcmp(argv[1], "gpio") == 0)
    {
      bk7258_gpio_probe();
      return 0;
    }

  if (strcmp(argv[1], "mtd") == 0)
    {
      return mtd_command();
    }

  if (strcmp(argv[1], "wdt") == 0)
    {
      return wdt_command(argc, argv);
    }

  usage();
  return 1;
}

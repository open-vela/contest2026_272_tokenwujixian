/****************************************************************************
 * BK7258 platform API and register readback test.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <nuttx/timers/timer.h>

#include <nuttx/arch.h>
#include <arch/chip/bk7258_clock.h>
#include <arch/chip/bk7258_gpio.h>
#include <arch/chip/bk7258_flash.h>
#include <bk7258_partition.h>
#include <arch/chip/bk7258_irq.h>
#include <arch/chip/bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>
#include <arch/chip/bk7258_timer.h>

#include <arch/chip/irq.h>

static volatile unsigned int g_irq_count;
static volatile unsigned int g_vendor_irq_count;

static void bk7258_platform_test_vendor_irq(void)
{
  g_vendor_irq_count++;
}

static void bk7258_platform_test_timer_signal(int signo)
{
  (void)signo;
  g_irq_count++;
}

struct bk7258_platform_test_snapshot_s
{
  uint32_t power_wakeup;
  uint32_t device_clock;
  uint32_t clkdiv1;
  uint32_t cpu0_int_en;
  uint32_t cpu0_int_en_hi;
  uint32_t gpio26_cfg;
  uint32_t gpio28_cfg;
  uint32_t gpio_func24_31;
};

static struct bk7258_platform_test_snapshot_s g_snapshot;

static uint32_t bk7258_platform_test_read(uintptr_t address)
{
  return *(volatile uint32_t *)address;
}

static void bk7258_platform_test_snapshot(void)
{
  g_snapshot.power_wakeup =
    bk7258_platform_test_read(BK7258_SYS_POWER_WAKEUP);
  g_snapshot.device_clock =
    bk7258_platform_test_read(BK7258_SYS_DEV_CLK_EN);
  g_snapshot.clkdiv1 =
    bk7258_platform_test_read(BK7258_SYS_CLKDIV1);
  g_snapshot.cpu0_int_en =
    bk7258_platform_test_read(BK7258_SYS_CPU0_INT_EN);
  g_snapshot.cpu0_int_en_hi =
    bk7258_platform_test_read(BK7258_SYS_CPU0_INT_EN_HI);
  g_snapshot.gpio26_cfg =
    bk7258_platform_test_read(BK7258_GPIO_CFG(26));
  g_snapshot.gpio28_cfg =
    bk7258_platform_test_read(BK7258_GPIO_CFG(28));
  g_snapshot.gpio_func24_31 =
    bk7258_platform_test_read(BK7258_SYS_GPIO_FUNC(26));
}

static void bk7258_platform_test_restore(void)
{
  *(volatile uint32_t *)BK7258_SYS_POWER_WAKEUP = g_snapshot.power_wakeup;
  *(volatile uint32_t *)BK7258_SYS_DEV_CLK_EN = g_snapshot.device_clock;
  *(volatile uint32_t *)BK7258_SYS_CLKDIV1 = g_snapshot.clkdiv1;
  *(volatile uint32_t *)BK7258_SYS_CPU0_INT_EN = g_snapshot.cpu0_int_en;
  *(volatile uint32_t *)BK7258_SYS_CPU0_INT_EN_HI = g_snapshot.cpu0_int_en_hi;
  *(volatile uint32_t *)BK7258_GPIO_CFG(26) = g_snapshot.gpio26_cfg;
  *(volatile uint32_t *)BK7258_GPIO_CFG(28) = g_snapshot.gpio28_cfg;
  *(volatile uint32_t *)BK7258_SYS_GPIO_FUNC(26) = g_snapshot.gpio_func24_31;
  __asm__ volatile ("dsb" : : : "memory");
}

static int bk7258_platform_test_irq(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;
  g_irq_count++;
  syslog(LOG_INFO, "[BK7258] platform test callback irq=%d count=%u\n",
         irq, g_irq_count);
  return OK;
}

static int check(const char *name, int ret)
{
  printf("[bk7258_platform_test] %-28s %s (%d)\n",
         name, ret == OK ? "PASS" : "FAIL", ret);
  return ret == OK ? 0 : 1;
}

static int test_power_clock(void)
{
  int failures = 0;
  uint32_t value;
  int ret;

  ret = bk7258_mac_power(true);
  failures += check("MAC power on", ret);
  value = bk7258_platform_test_read(BK7258_SYS_POWER_WAKEUP);
  failures += check("MAC power readback",
                    (value & BK7258_SYS_WIFI_MAC_POWERDOWN) == 0 ? OK : -EIO);

  ret = bk7258_phy_power(true);
  failures += check("PHY power on", ret);
  value = bk7258_platform_test_read(BK7258_SYS_POWER_WAKEUP);
  failures += check("PHY power readback",
                    (value & BK7258_SYS_WIFI_PHY_POWERDOWN) == 0 ? OK : -EIO);

  ret = bk7258_mac_clock(true);
  failures += check("MAC clock on", ret);
  value = bk7258_platform_test_read(BK7258_SYS_DEV_CLK_EN);
  failures += check("MAC clock readback",
                    (value & BK7258_SYS_MAC_CKEN) != 0 ? OK : -EIO);

  ret = bk7258_phy_clock(true);
  failures += check("PHY clock on", ret);
  value = bk7258_platform_test_read(BK7258_SYS_DEV_CLK_EN);
  failures += check("PHY clock readback",
                    (value & BK7258_SYS_PHY_CKEN) != 0 ? OK : -EIO);

  failures += check("MAC reset unsupported",
                    bk7258_mac_reset() == -ENOTSUP ? OK : -EIO);
  failures += check("PHY reset unsupported",
                    bk7258_phy_reset() == -ENOTSUP ? OK : -EIO);

  ret = bk7258_phy_clock(false);
  failures += check("PHY clock off", ret);
  value = bk7258_platform_test_read(BK7258_SYS_DEV_CLK_EN);
  failures += check("PHY clock off readback",
                    (value & BK7258_SYS_PHY_CKEN) == 0 ? OK : -EIO);
  ret = bk7258_mac_clock(false);
  failures += check("MAC clock off", ret);
  value = bk7258_platform_test_read(BK7258_SYS_DEV_CLK_EN);
  failures += check("MAC clock off readback",
                    (value & BK7258_SYS_MAC_CKEN) == 0 ? OK : -EIO);
  ret = bk7258_phy_power(false);
  failures += check("PHY power off", ret);
  value = bk7258_platform_test_read(BK7258_SYS_POWER_WAKEUP);
  failures += check("PHY power off readback",
                    (value & BK7258_SYS_WIFI_PHY_POWERDOWN) != 0 ? OK : -EIO);
  ret = bk7258_mac_power(false);
  failures += check("MAC power off", ret);
  value = bk7258_platform_test_read(BK7258_SYS_POWER_WAKEUP);
  failures += check("MAC power off readback",
                    (value & BK7258_SYS_WIFI_MAC_POWERDOWN) != 0 ? OK : -EIO);
  return failures;
}

static int test_flash_controller(void)
{
  uint8_t first[32];
  uint8_t second[32];
  uint32_t id;
  int failures = 0;

  failures += check("Flash initialize", bk7258_flash_initialize());
  failures += check("Flash ID read", bk7258_flash_read_id(&id));
  failures += check("Flash ID valid", id != 0 && id != UINT32_MAX ? OK : -EIO);
  failures += check("Flash read offset 0", bk7258_flash_read(0, first,
                                                               sizeof(first)));
  failures += check("Flash read repeat", bk7258_flash_read(0, second,
                                                             sizeof(second)));
  failures += check("Flash read stable",
                    memcmp(first, second, sizeof(first)) == 0 ? OK : -EIO);
  failures += check("Flash read CP slot",
                    bk7258_flash_read(UINT32_C(0x11000), second,
                                      sizeof(second)));
  failures += check("Flash NULL buffer",
                    bk7258_flash_read(0, NULL, sizeof(first)) == -EINVAL ?
                    OK : -EIO);
  failures += check("Flash invalid offset",
                    bk7258_flash_read(BK7258_FLASH_SIZE_BYTES, first, 1) ==
                    -ERANGE ? OK : -EIO);
  failures += check("Flash invalid length",
                    bk7258_flash_read(BK7258_FLASH_SIZE_BYTES - 1, first, 2) ==
                    -ERANGE ? OK : -EIO);
  printf("[bk7258_platform_test] Flash ID=0x%08lx first=0x%02x%02x%02x%02x\n",
         (unsigned long)id, first[0], first[1], first[2], first[3]);
  return failures;
}

static int test_flash_partitions(void)
{
  uint8_t rf[32];
  uint8_t net[32];
  uint32_t start;
  uint32_t length;
  int failures = 0;

  failures += check("SYS_RF info",
                    bk7258_partition_get_info(BK7258_PARTITION_SYS_RF,
                                               &start, &length));
  failures += check("SYS_RF layout",
                    start == BK7258_PART_SYS_RF_OFFSET &&
                    length == BK7258_PART_SYS_RF_SIZE ? OK : -EIO);
  failures += check("SYS_NET info",
                    bk7258_partition_get_info(BK7258_PARTITION_SYS_NET,
                                               &start, &length));
  failures += check("SYS_NET layout",
                    start == BK7258_PART_SYS_NET_OFFSET &&
                    length == BK7258_PART_SYS_NET_SIZE ? OK : -EIO);
  failures += check("SYS_RF read",
                    bk7258_partition_read(BK7258_PARTITION_SYS_RF, rf, 0,
                                          sizeof(rf)));
  failures += check("SYS_NET read",
                    bk7258_partition_read(BK7258_PARTITION_SYS_NET, net, 0,
                                          sizeof(net)));
  failures += check("SYS_RF boundary",
                    bk7258_partition_read(BK7258_PARTITION_SYS_RF, rf,
                                           BK7258_PART_SYS_RF_SIZE - 1,
                                           2) == -ERANGE ? OK : -EIO);
  failures += check("SYS_NET invalid",
                    bk7258_partition_read(BK7258_PARTITION_SYS_NET, net,
                                           BK7258_PART_SYS_NET_SIZE,
                                           1) == -ERANGE ? OK : -EIO);
  failures += check("partition unknown",
                    bk7258_partition_get_info(0, &start, &length) == -ENOENT ?
                    OK : -EIO);
  printf("[bk7258_platform_test] SYS_RF first=0x%02x SYS_NET first=0x%02x\n",
         rf[0], net[0]);
  return failures;
}

static int test_pmu_readback(void)
{
  uint32_t raw7c;
  uint32_t raw7d;
  uint32_t analog8;
  uint32_t analog9;
  uint32_t value;
  uint32_t latch_value;
  int failures = 0;

  failures += check("PMU R7C read", bk7258_pmu_read(0x7c, &raw7c));
  failures += check("PMU R7D read", bk7258_pmu_read(0x7d, &raw7d));
  failures += check("PMU chipid read", bk7258_pmu_get_chipid(&value));
  failures += check("PMU chipid width", value != 0 ? OK : -EIO);
  failures += check("PMU ADC cal read", bk7258_pmu_get_adc_cal(&value));
  failures += check("PMU ADC cal width", value <= UINT32_C(0x3f) ? OK : -EIO);
  failures += check("PMU BG cal read", bk7258_pmu_get_bgcal(&value));
  failures += check("PMU BG cal width", value <= UINT32_C(0x3f) ? OK : -EIO);
  failures += check("analog R0 read", bk7258_analog_read(0, &value));
  failures += check("analog R8 read", bk7258_analog_read(8, &analog8));
  failures += check("analog R9 read", bk7258_analog_read(9, &analog9));
  failures += check("analog BG read", bk7258_sys_get_bgcalm(&value));
  printf("[bk7258_platform_test] analog BGCAL=%lu\n",
         (unsigned long)value);

  /* Validate a real analog-bus write using the latch gate itself.  Changing
   * BGCAL or DPLL while executing from Flash can destabilize the running
   * system before the test can restore it, so those destructive transitions
   * belong in a dedicated reset-bounded calibration test. */
  failures += check("analog latch set",
                    bk7258_analog_write(9, analog9 | (UINT32_C(1) << 9)));
  failures += check("analog latch readback",
                    bk7258_analog_read(9, &latch_value));
  failures += check("analog latch behavior",
                    (latch_value & (UINT32_C(1) << 9)) != 0 ? OK : -EIO);
  failures += check("analog latch restore",
                    bk7258_analog_write(9, analog9));
  failures += check("analog latch restored",
                    bk7258_analog_read(9, &latch_value));
  failures += check("analog latch state",
                    latch_value == analog9 ? OK : -EIO);
  failures += check("analog invalid read",
                    bk7258_analog_read(28, &value) == -EINVAL ? OK : -EIO);
  printf("[bk7258_platform_test] PMU R7C=0x%08lx R7D=0x%08lx\n",
         (unsigned long)raw7c, (unsigned long)raw7d);
  return failures;
}

static int test_irq_map(void)
{
  static const unsigned int sources[] =
    {
      29, 30, 31, 32, 33, 34, 35, 36, 37, 38
    };
  unsigned int i;
  int failures = 0;

  for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++)
    {
      unsigned int source = sources[i];
      uintptr_t reg = source < 32 ? BK7258_SYS_CPU0_INT_EN :
                      BK7258_SYS_CPU0_INT_EN_HI;
      uint32_t mask = source < 32 ? BK7258_SYS_IRQ_GROUP0(source) :
                      BK7258_SYS_IRQ_GROUP1(source);
      char name[32];
      int ret;

      snprintf(name, sizeof(name), "ICU %u attach", source);
      ret = bk7258_icu_attach(source, bk7258_platform_test_irq, NULL);
      failures += check(name, ret);
      snprintf(name, sizeof(name), "ICU %u enable", source);
      ret = bk7258_icu_enable(source);
      failures += check(name, ret);
      snprintf(name, sizeof(name), "ICU %u bit readback", source);
      failures += check(name,
                        (bk7258_platform_test_read(reg) & mask) != 0 ?
                        OK : -EIO);
      snprintf(name, sizeof(name), "ICU %u disable", source);
      ret = bk7258_icu_disable(source);
      failures += check(name, ret);
      snprintf(name, sizeof(name), "ICU %u bit clear", source);
      failures += check(name,
                        (bk7258_platform_test_read(reg) & mask) == 0 ?
                        OK : -EIO);
      irq_detach(BK7258_IRQ_TIMER0 - 3 + source);
    }

  failures += check("ICU invalid source",
                    bk7258_icu_enable(64) == -EINVAL ? OK : -EIO);
  failures += check("ICU NULL handler",
                    bk7258_icu_attach(29, NULL, NULL) == -EINVAL ? OK : -EIO);
  return failures;
}

static int test_vendor_irq_api(void)
{
  int failures = 0;
  int ret;

  g_vendor_irq_count = 0;
  ret = bk7258_icu_vendor_register(3, bk7258_platform_test_vendor_irq,
                                   NULL);
  failures += check("vendor IRQ register", ret);
  failures += check("vendor IRQ duplicate",
                    bk7258_icu_vendor_register(3,
                                                bk7258_platform_test_vendor_irq,
                                                NULL) == -EBUSY ? OK : -EIO);
  failures += check("vendor IRQ priority",
                    bk7258_icu_set_priority(3, 128));
  failures += check("vendor IRQ enable", bk7258_icu_enable(3));
  irq_dispatch(BK7258_IRQ_TIMER0, NULL);
  failures += check("vendor IRQ software dispatch",
                    g_vendor_irq_count == 1 ? OK : -EIO);
  failures += check("vendor IRQ disable", bk7258_icu_disable(3));
  failures += check("vendor IRQ unregister", bk7258_icu_vendor_unregister(3));
  failures += check("vendor IRQ invalid",
                    bk7258_icu_vendor_register(64,
                                                bk7258_platform_test_vendor_irq,
                                                NULL) == -EINVAL ? OK : -EIO);
  return failures;
}

static int test_gpio_mux(void)
{
  int failures = 0;
  uint32_t value;
  int ret;

  ret = bk7258_gpio_wifi_txen();
  failures += check("GPIO26 TXEN mux", ret);
  value = bk7258_platform_test_read(BK7258_GPIO_CFG(26));
  failures += check("GPIO26 TXEN ownership",
                    (value & BK7258_GPIO_CFG_SECOND_FUNC) != 0 ? OK : -EIO);
  value = bk7258_platform_test_read(BK7258_SYS_GPIO_FUNC(26));
  failures += check("GPIO26 TXEN selector",
                    (value & BK7258_GPIO_FUNC_MASK(26)) == 0 ? OK : -EIO);

  ret = bk7258_gpio_wifi_rxen();
  failures += check("GPIO28 RXEN mux", ret);
  value = bk7258_platform_test_read(BK7258_GPIO_CFG(28));
  failures += check("GPIO28 RXEN ownership",
                    (value & BK7258_GPIO_CFG_SECOND_FUNC) != 0 ? OK : -EIO);
  value = bk7258_platform_test_read(BK7258_SYS_GPIO_FUNC(28));
  failures += check("GPIO28 RXEN selector",
                    (value & BK7258_GPIO_FUNC_MASK(28)) == 0 ? OK : -EIO);
  return failures;
}

static int test_timer0_irq(void)
{
  int fd;
  struct timer_notify_s notify;
  struct sigaction action;
  unsigned int before = g_irq_count;
  unsigned int count;
  uint32_t ctrl;
  uint32_t sysclk;
  int ret;

  memset(&action, 0, sizeof(action));
  action.sa_handler = bk7258_platform_test_timer_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGALRM, &action, NULL);
  fd = open("/dev/timer0", O_RDONLY);
  ret = fd < 0 ? -errno : OK;
  if (check("Timer0 open", ret) != 0)
    {
      return 1;
    }
  ret = ioctl(fd, TCIOC_SETTIMEOUT, 10000);
  ret = check("Timer0 set timeout", ret);
  memset(&notify, 0, sizeof(notify));
  notify.pid = getpid();
  notify.periodic = true;
  notify.event.sigev_notify = SIGEV_SIGNAL;
  notify.event.sigev_signo = SIGALRM;
  notify.event.sigev_value.sival_ptr = NULL;
  ret += check("Timer0 notification", ioctl(fd, TCIOC_NOTIFICATION,
                                             (unsigned long)&notify));
  ret += check("Timer0 start", ioctl(fd, TCIOC_START, 0));
  if (ret != 0)
    {
      close(fd);
      return 1;
    }

  ctrl = bk7258_platform_test_read(BK7258_TIMER0_CTRL);
  sysclk = bk7258_platform_test_read(BK7258_SYS_CLKDIV1);
  printf("[bk7258_platform_test] Timer0 ctrl=0x%08lx sysclk=0x%08lx\n",
         (unsigned long)ctrl, (unsigned long)sysclk);

  for (count = 0; count < 100 && g_irq_count == before; count++)
    {
      up_mdelay(1);
    }

  ctrl = bk7258_platform_test_read(BK7258_TIMER0_CTRL);
  printf("[bk7258_platform_test] Timer0 ctrl-after=0x%08lx irq_count=%u\n",
         (unsigned long)ctrl, g_irq_count);

  ret = ioctl(fd, TCIOC_STOP, 0);
  int failures = check("Timer0 stop", ret);
  ctrl = bk7258_platform_test_read(BK7258_TIMER0_CTRL);
  printf("[bk7258_platform_test] Timer0 ctrl-stop=0x%08lx\n",
         (unsigned long)ctrl);
  failures += check("Timer0 status clear",
                    (ctrl & BK7258_TIMER0_INT_ENABLE) == 0 ? OK : -EIO);
  close(fd);
  failures += check("Timer0 NuttX handler",
                    g_irq_count > before ? OK : -ETIMEDOUT);
  return failures;
}

int main(int argc, char *argv[])
{
  int failures = 0;

  (void)argc;
  (void)argv;
  printf("[bk7258_platform_test] begin\n");
  bk7258_platform_test_snapshot();
  failures += test_power_clock();
  failures += test_flash_controller();
  failures += test_flash_partitions();
  failures += test_pmu_readback();
  failures += test_irq_map();
  failures += test_vendor_irq_api();
  failures += test_timer0_irq();
  failures += test_gpio_mux();
  bk7258_platform_test_restore();
  printf("[bk7258_platform_test] register state restored\n");
  printf("[bk7258_platform_test] %s failures=%d irq_count=%u\n",
         failures == 0 ? "PASS" : "FAIL", failures, g_irq_count);
  return failures == 0 ? 0 : 1;
}

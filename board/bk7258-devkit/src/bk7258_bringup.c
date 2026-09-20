/****************************************************************************
 * board/bk7258-devkit/src/bk7258_bringup.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <sys/mount.h>

#include <nuttx/board.h>
#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/semaphore.h>
#include <nuttx/serial/uart_rpmsg.h>
#include <nuttx/signal.h>
#ifdef CONFIG_NET_RPMSG_DRV
#  include <nuttx/net/rpmsgdrv.h>
#  include <nuttx/net/netdev.h>
#  include <netinet/in.h>
#endif

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <nuttx/input/buttons.h>
#endif

#include <arch/chip/bk7258_ap_boot.h>
#include <arch/chip/bk7258_mb_ipc.h>
#include <arch/chip/bk7258_memorymap.h>
#include <arch/chip/bk7258_timer.h>
#include <arch/chip/bk7258_wifi.h>
#include <arch/board/board.h>

#ifdef CONFIG_RPTUN
#  include "bk7258_rptun.h"
#endif

#ifdef CONFIG_AUDIO_BK7258
#  include <arch/chip/bk7258_audio.h>
int bk7258_devkit_audio_pa_register(void);
#endif

#ifdef CONFIG_BK7258_COMPONENT_CP
int bk7258_ap_start_monitor(void);

#ifdef CONFIG_BK7258_WIFI_VENDOR_RUNTIME
#  define BK7258_WIFI_INIT_PRIORITY  85
#  define BK7258_WIFI_INIT_STACKSIZE 8192
#  define BK7258_WIFI_INIT_TIMEOUT   SEC2TICK(30)

static sem_t g_bk7258_wifi_init_done;
static int g_bk7258_wifi_init_result;

static int bk7258_wifi_init_worker(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  g_bk7258_wifi_init_result = bk7258_wifi_initialize();
  nxsem_post(&g_bk7258_wifi_init_done);
  return g_bk7258_wifi_init_result;
}

static void bk7258_wifi_boot_initialize(void)
{
  int ret;

  ret = nxsem_init(&g_bk7258_wifi_init_done, 0, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] init semaphore failed: %d\n", ret);
      return;
    }

  ret = kthread_create("bk7258-wifi-init", BK7258_WIFI_INIT_PRIORITY,
                       BK7258_WIFI_INIT_STACKSIZE,
                       bk7258_wifi_init_worker, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] init thread failed: %d\n", ret);
      return;
    }

  ret = nxsem_tickwait_uninterruptible(&g_bk7258_wifi_init_done,
                                       BK7258_WIFI_INIT_TIMEOUT);
  if (ret == -ETIMEDOUT)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] init timed out after 30 seconds\n");
      return;
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] init wait failed: %d\n", ret);
    }
  else if (g_bk7258_wifi_init_result < 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] boot init failed: %d\n",
             g_bk7258_wifi_init_result);
    }
  else
    {
      syslog(LOG_INFO, "[BK7258-WIFI] wlan0 ready before userspace\n");
    }
}
#endif
#endif

#ifdef CONFIG_BK7258_COMPONENT_AP
static void bk7258_ap_initialize(void);
#endif

#ifdef CONFIG_RPMSG_UART
void rpmsg_serialinit(void)
{
  int ret;

#ifdef CONFIG_BK7258_COMPONENT_CP
  up_putc('r');
  ret = uart_rpmsg_init("ap", "AP", 4096, false);
  up_putc('o');
#else
#  ifdef CONFIG_RPMSG_UART_CONSOLE
  ret = uart_rpmsg_init("cp", "AP", 4096, true);
#  else
  ret = uart_rpmsg_init("cp", "AP", 4096, false);
#  endif
#endif

  if (ret < 0)
    {
      syslog(LOG_ERR, "[AMP] RPMsg UART init failed: %d\n", ret);
#ifdef CONFIG_BK7258_COMPONENT_AP
      bk7258_ap_record_fault(UINT32_C(0x55525400) |
                             ((uint32_t)-ret & UINT32_C(0xff)));
#endif
    }
}
#endif

#if defined(CONFIG_LCD_JD9853) || defined(CONFIG_LCD_GC9D01)
#  include <nuttx/lcd/lcd_dev.h>
#endif

void board_late_initialize(void)
{
  /* UART console registration is chip-owned. Board peripherals are added
   * only after their pinmux and hardware contracts are verified. */

  /* Diagnostic-only boot ladder. /dev/console has been registered by the
   * time board_late_initialize runs; up_putc remains independent of syslog.
   */
  up_putc('K');

#ifdef CONFIG_BK7258_COMPONENT_CP
#ifdef CONFIG_BK7258_TIMER0
  {
    int timer_ret = bk7258_timer_initialize();
    if (timer_ret < 0)
      {
        syslog(LOG_ERR, "[BK7258] Timer0 lower-half init failed: %d\n",
               timer_ret);
      }
    else
      {
        syslog(LOG_INFO, "[BK7258] Timer0 lower-half registered\n");
      }

    up_putc('L');
  }
#endif
#ifdef CONFIG_RPTUN
  int ret = bk7258_rptun_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[AMP] CP RPTUN init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "[AMP] CP RPTUN master initialized (Mailbox IRQ)\n");
#ifdef CONFIG_NET_RPMSG_DRV
      /* Cross-core Ethernet-over-RPMsg netdev; the AP side registers its
       * peer from bk7258_ap_amp_initialize() after its own RPTUN is up.
       * The endpoint name is derived from the netdev name (rpmsgdrv.c,
       * NET_RPMSG_EPT_PREFIX), so both sides must agree on "rpmsg0".
       * NET_LL_IEEE80211 keeps the 576-byte MTU contract of wlan0 instead
       * of pulling in the 1500-byte Ethernet default on this RAM-tight
         build. */

      FAR struct net_driver_s *rpmsg0;
      FAR struct netdev_lowerhalf_s *lower;

      lower = net_rpmsg_drv_init("ap", "rpmsg0", NET_LL_IEEE80211);
      if (lower == NULL)
        {
          syslog(LOG_ERR, "[AMP] CP rpmsg0 netdev init failed\n");
        }
      else
        {
          syslog(LOG_INFO, "[AMP] CP rpmsg0 netdev registered\n");

          /* Static 192.168.7.1/24 on the CP side of the link.  Kernel-side
           * assignment (d_ipaddr/d_netmask under net_lock, then ifup) is
           * what SIOCSIFADDR/SIOCSIFNETMASK would do from userspace, minus
           * the rcS file system dependency.  These addresses are a board
           * contract between the two components, not a user preference. */

          rpmsg0 = netdev_findbyname("rpmsg0");
          if (rpmsg0 != NULL)
            {
              net_lock();
              rpmsg0->d_ipaddr   = htonl(0xc0a80701); /* 192.168.7.1 */
              rpmsg0->d_netmask  = htonl(0xffffff00); /* 255.255.255.0 */
              net_unlock();
              netdev_ifup(rpmsg0);
              syslog(LOG_INFO, "[AMP] CP rpmsg0 192.168.7.1/24 up\n");
            }
        }
#endif
#ifdef CONFIG_BK7258_MB_IPC_RPMSG
      ret = bk7258_mb_ipc_initialize();
      if (ret != 0)
        {
          syslog(LOG_ERR, "[AMP] CP mailbox IPC init failed: %d\n", ret);
        }
#endif
    }

  up_putc('M');
#else
  (void)bk7258_ap_start_monitor();
#endif
#ifdef CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  bk7258_wifi_boot_initialize();
#endif
#else
  bk7258_ap_initialize();
#endif
}

int board_app_initialize(uintptr_t arg)
{
#ifdef CONFIG_FS_PROCFS
  /* nshlib does not mount procfs by itself, and neither component has a startup
   * script (CONFIG_NSH_ROMFSETC is unset for both): mount it here so `ps` and
   * the per-task affinity view work in the RPMsg-backed AP NSH.
   *
   * No longer restricted to the AP.  On the CP this is what makes `ifconfig`
   * usable at all: nsh_netcmds.c reads /proc/net to enumerate devices (:373)
   * and per-device state (:189), so without the mount the command exists but
   * fails with "is procfs mounted?".  That is also why
   * NSH_DISABLE_IFCONFIG/IFUPDOWN default to y when FS_PROCFS is off -- the
   * commands are useless without it. */

  if (mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL) < 0 &&
      get_errno() != EEXIST)
    {
      syslog(LOG_ERR, "[BK7258] procfs mount failed: %d\n", get_errno());
    }
#endif

  /* CONFIG_NSH_ARCHINIT calls this from the NSH startup path. */
  up_putc('N');

#if defined(CONFIG_LCD_JD9853) || defined(CONFIG_LCD_GC9D01)
  int ret;

  ret = board_lcd_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = lcddev_register(0);
  if (ret < 0)
    {
      return ret;
    }
#endif

  (void)arg;
  return 0;
}

#ifdef CONFIG_BK7258_COMPONENT_AP
/* BK7258 AON GPIO configuration bits. GPIO output enable is active-low. */

#define BK7258_GPIO_OUTPUT_VALUE  (UINT32_C(1) << 1)
#define BK7258_GPIO_INPUT_ENABLE  (UINT32_C(1) << 2)
#define BK7258_GPIO_OUTPUT_DISABLE (UINT32_C(1) << 3)
#define BK7258_GPIO_PULL_MODE     (UINT32_C(1) << 4)
#define BK7258_GPIO_PULL_ENABLE   (UINT32_C(1) << 5)
#define BK7258_GPIO_SECOND_FUNC   (UINT32_C(1) << 6)

static inline uint32_t bk7258_ap_gpio_read(unsigned int pin)
{
  return *(volatile uint32_t *)BK7258_GPIO_CFG(pin);
}

static inline void bk7258_ap_gpio_write(unsigned int pin, uint32_t value)
{
  *(volatile uint32_t *)BK7258_GPIO_CFG(pin) = value;
  __asm__ volatile ("dsb" : : : "memory");
}

static void bk7258_ap_heartbeat_led_write(bool on)
{
  uint32_t config =
    bk7258_ap_gpio_read(BOARD_AP_HEARTBEAT_LED_PIN);
  bool high = BOARD_AP_HEARTBEAT_LED_ACTIVE_HIGH ? on : !on;

  if (high)
    {
      config |= BK7258_GPIO_OUTPUT_VALUE;
    }
  else
    {
      config &= ~BK7258_GPIO_OUTPUT_VALUE;
    }

  bk7258_ap_gpio_write(BOARD_AP_HEARTBEAT_LED_PIN, config);
}

static void bk7258_ap_heartbeat_led_initialize(void)
{
  uint32_t config =
    bk7258_ap_gpio_read(BOARD_AP_HEARTBEAT_LED_PIN);

  /* Select ordinary GPIO, disable input and pulls, enable output, and seed the
   * inactive level before the task starts toggling the pin. */

  config &= ~(BK7258_GPIO_OUTPUT_VALUE |
              BK7258_GPIO_INPUT_ENABLE |
              BK7258_GPIO_OUTPUT_DISABLE |
              BK7258_GPIO_PULL_MODE |
              BK7258_GPIO_PULL_ENABLE |
              BK7258_GPIO_SECOND_FUNC);
  bk7258_ap_gpio_write(BOARD_AP_HEARTBEAT_LED_PIN, config);
  bk7258_ap_heartbeat_led_write(false);
}

static int bk7258_ap_panic_notify(struct notifier_block *block,
                                  unsigned long action, void *data)
{
  FAR struct panic_notifier_s *info = data;
  uint32_t fault;

  (void)block;

  /* PANIC_TASK can describe a task-local assertion. Only kernel panic stages
   * are fatal evidence for this AP instance. */

  if (action != PANIC_TASK)
    {
      /* Encode the assertion location into the fault word so the CP-side
       * monitor can point at the failing source: high byte = a short hash of
       * the filename, low byte = line number. */

      if (info != NULL && info->filename != NULL)
        {
          uint32_t hash = 0;
          FAR const char *p;

          for (p = info->filename; *p != '\0'; p++)
            {
              hash = (hash << 1) ^ (hash >> 31) ^ (uint32_t)*p;
            }

          fault = BK7258_AP_FAULT_PANIC_BASE |
                  ((hash & UINT32_C(0xff)) << 8) |
                  ((uint32_t)(info->linenum & UINT32_C(0xff)));
        }
      else
        {
          fault = BK7258_AP_FAULT_PANIC_BASE |
                  ((uint32_t)action & UINT32_C(0xff));
        }

      bk7258_ap_record_fault(fault);
    }

  return 0;
}

static struct notifier_block g_bk7258_ap_panic_notifier =
{
  .notifier_call = bk7258_ap_panic_notify,
  .priority = 100,
};

#ifdef CONFIG_RPTUN
static int bk7258_ap_amp_initialize(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  ret = bk7258_rptun_initialize();
  if (ret < 0)
    {
      /* Keep the heartbeat task alive so the SWAP record still proves AP
       * liveness even when AMP initialization fails.
       */

      return ret;
    }

#ifdef CONFIG_BK7258_MB_IPC_RPMSG
  ret = bk7258_mb_ipc_initialize();
  if (ret != 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_NET_RPMSG_DRV
  /* Peer of the CP-side rpmsg0 created in board_late_initialize(); the
   * endpoint is announced through RPMsg name service and bound on the
   * other core.  Must run after this core's RPTUN is up (this kthread)
   * and before userspace starts issuing ifconfig/DHCP on rpmsg0. */

  FAR struct net_driver_s *rpmsg0;
  FAR struct netdev_lowerhalf_s *lower;

  lower = net_rpmsg_drv_init("cp", "rpmsg0", NET_LL_IEEE80211);
  if (lower == NULL)
    {
      syslog(LOG_ERR, "[AMP] AP rpmsg0 netdev init failed\n");
    }
  else
    {
      syslog(LOG_INFO, "[AMP] AP rpmsg0 netdev registered\n");

      /* Static 192.168.7.2/24, the AP side of the board contract; see the
       * matching comment on the CP side for why this is kernel-side. */

      rpmsg0 = netdev_findbyname("rpmsg0");
      if (rpmsg0 != NULL)
        {
          net_lock();
          rpmsg0->d_ipaddr   = htonl(0xc0a80702); /* 192.168.7.2 */
          rpmsg0->d_netmask  = htonl(0xffffff00); /* 255.255.255.0 */
          net_unlock();
          netdev_ifup(rpmsg0);
          syslog(LOG_INFO, "[AMP] AP rpmsg0 192.168.7.2/24 up\n");
        }
    }
#endif

  /* Publish scheduler-running only after the AP-side RPTUN/RPMsg instance is
   * initialized.  CP uses the SWAP generation transition as the trigger to
   * tear down and rebuild its stale remote transport, so advertising this
   * stage earlier would let CP restart AP while its new RPTUN instance was
   * still coming up. */

  bk7258_ap_record_mark_scheduler();

  return 0;
}
#endif

static int bk7258_ap_health_thread(int argc, char *argv[])
{
  bool led_on = true;
  int ret;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      ret = nxsig_usleep(1000 * 1000);
      if (ret < 0)
        {
          bk7258_ap_record_fault(BK7258_AP_FAULT_SLEEP_BASE |
                                 ((uint32_t)-ret & UINT32_C(0xff)));
          bk7258_ap_heartbeat_led_write(false);
          return ret;
        }

      led_on = !led_on;
      bk7258_ap_heartbeat_led_write(led_on);
      bk7258_ap_record_heartbeat();
    }

  return 0;
}

static void bk7258_ap_initialize(void)
{
  int ret;

  /* board_late_initialize() runs before nsh_main. Keep board health and AMP
   * setup in kernel workers so the standard NSH entry can own /dev/console.
   * The notifier is registered here, after tasklist_initialize(): earlier
   * registration would dereference a NULL TCB inside sched_lock().
   */

  syslog(LOG_INFO, "BK7258: bk7258_ap_initialize called\n");

  panic_notifier_chain_register(&g_bk7258_ap_panic_notifier);
  bk7258_ap_heartbeat_led_initialize();
  bk7258_ap_heartbeat_led_write(true);

  /* Initialize audio devices first (before RPTUN which may return early) */

#ifdef CONFIG_AUDIO_BK7258
  syslog(LOG_INFO, "BK7258: Initializing audio devices...\n");

  /* Register the DevKit PA control ops before the playback device is
   * created so the driver never runs with an unknown PA state.
   */

  ret = bk7258_devkit_audio_pa_register();
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 PA registration failed: %d; "
                      "playback device disabled\n", ret);
    }
  else
    {
      ret = bk7258_audio_playback_initialize();
      if (ret < 0)
        {
          syslog(LOG_ERR, "BK7258 playback audio init failed: %d\n", ret);
        }
      else
        {
          syslog(LOG_INFO, "BK7258: Playback device registered OK\n");
        }
    }

#ifdef CONFIG_AUDIO_BK7258_CAPTURE
  ret = bk7258_audio_capture_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 capture audio init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "BK7258: Capture device registered OK\n");
    }
#endif
#endif

  /* Initialize buttons (non-fatal if it fails) */

#ifdef CONFIG_INPUT_BUTTONS_LOWER
  ret = btn_lower_initialize("/dev/btn0");
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 button init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "BK7258: /dev/btn0 registered OK\n");
    }
#endif

#ifdef CONFIG_RPTUN
  ret = kthread_create("bk7258-amp-init", 95, 8192,
                       bk7258_ap_amp_initialize, NULL);
  if (ret < 0)
    {
      bk7258_ap_record_fault(UINT32_C(0x52505400) |
                             ((uint32_t)-ret & UINT32_C(0xff)));
      bk7258_ap_heartbeat_led_write(false);
    }
#endif

  ret = kthread_create("bk7258-ap-health", 80, 2048,
                       bk7258_ap_health_thread, NULL);
  if (ret < 0)
    {
      bk7258_ap_record_fault(UINT32_C(0x48425400) |
                             ((uint32_t)-ret & UINT32_C(0xff)));
      bk7258_ap_heartbeat_led_write(false);
    }
}
#endif

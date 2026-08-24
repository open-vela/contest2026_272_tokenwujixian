/****************************************************************************
 * board/bk7258-devkit/src/bk7258_bringup.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/board.h>
#include <nuttx/kthread.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/serial/uart_rpmsg.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_ap_boot.h>
#include <arch/chip/bk7258_memorymap.h>
#include <arch/chip/bk7258_timer.h>
#include <arch/board/board.h>

#ifdef CONFIG_RPTUN
#  include "bk7258_rptun.h"
#endif

#ifdef CONFIG_BK7258_COMPONENT_CP
int bk7258_ap_start_monitor(void);
#endif

#ifdef CONFIG_BK7258_COMPONENT_AP
static void bk7258_ap_initialize(void);
#endif

#ifdef CONFIG_RPMSG_UART
void rpmsg_serialinit(void)
{
  int ret;

#ifdef CONFIG_BK7258_COMPONENT_CP
  ret = uart_rpmsg_init("ap", "AP", 4096, false);
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
    }
#else
  (void)bk7258_ap_start_monitor();
#endif
#else
  bk7258_ap_initialize();
#endif
}

int board_app_initialize(uintptr_t arg)
{
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
  (void)block;
  (void)data;

  /* PANIC_TASK can describe a task-local assertion. Only kernel panic stages
   * are fatal evidence for this AP instance. */

  if (action != PANIC_TASK)
    {
      bk7258_ap_record_fault(BK7258_AP_FAULT_PANIC_BASE |
                             ((uint32_t)action & UINT32_C(0xff)));
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
      /* Keep the heartbeat task alive: absence of /dev/rpmsg/ap then isolates
       * the failure to AMP without losing the already verified AP liveness
       * channel. */

      return ret;
    }

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
   */

  panic_notifier_chain_register(&g_bk7258_ap_panic_notifier);
  bk7258_ap_heartbeat_led_initialize();
  bk7258_ap_heartbeat_led_write(true);
  bk7258_ap_record_mark_scheduler();

#ifdef CONFIG_RPTUN
  ret = kthread_create("bk7258-amp-init", 95, 4096,
                       bk7258_ap_amp_initialize, NULL);
  if (ret < 0)
    {
      bk7258_ap_record_fault(UINT32_C(0x52505400) |
                             ((uint32_t)-ret & UINT32_C(0xff)));
      bk7258_ap_heartbeat_led_write(false);
      return;
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

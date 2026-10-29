/****************************************************************************
 * board/bk7258-devkit/src/bk7258_bringup.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <sys/mount.h>

#include <arch/irq.h>

#include <nuttx/board.h>
#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/serial/uart_rpmsg.h>
#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <nuttx/syslog/syslog.h>

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <nuttx/input/buttons.h>
int bk7258_buttons_input_initialize(void);
int bk7258_status_led_initialize(void);
#endif

#include <arch/irq.h>
#include <arch/chip/bk7258_ap_boot.h>
#include <arch/chip/bk7258_irq.h>
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
void bk7258_ca_init(void);
#endif

/* chips/bk7258/bk7258_trng.c.  Initialised here rather than from Armino's
 * driver_init() (which gates it on CONFIG_TRNG_SUPPORT), so the unit does
 * not depend on the Armino registry for its own lifecycle.
 */
int bk_trng_driver_init(void);

/* chips/bk7258/wlan_ctrl/wlan_ctrl_server.c -- the CP half of the
 * cross-core Wi-Fi control plane.  Needs RPMsg up first.
 */
int wlan_ctrl_server_init(void);

/* Exceptions 15 and up (SysTick and every peripheral IRQ) enter through
 * exception_direct (arm_vectors.c, bk7258_smp.c _vectors_core1), which does
 * not save the interrupted context into xcp.regs.  A panic raised there --
 * the RPTUN peer-panic kick from the other core is one -- made the NuttX
 * dump read a stale saved SP, and the unwinder walked off the task stack
 * into a precise BusFault on both cores.  Rebuild the interrupted context
 * from the hardware frame on PSP (r0-r3, r12, lr, pc, xpsr lead both the
 * basic and the FP extended frame) into a private buffer.  Nothing is
 * scheduled after a panic, so repointing xcp.regs only affects the dump. */

#define BK7258_EXC_DIRECT_FIRST 15

static void bk7258_panic_fix_irq_regs(void)
{
  static uint32_t regs[XCPTCONTEXT_REGS];
  FAR struct tcb_s *rtcb;
  FAR uint32_t *frame;
  uintptr_t base;
  uintptr_t top;
  uint32_t psp;

  if (!up_interrupt_context() || getipsr() < BK7258_EXC_DIRECT_FIRST)
    {
      return;
    }

  rtcb = running_task();
  base = (uintptr_t)rtcb->stack_base_ptr;
  top = base + rtcb->adj_stack_size;

  __asm__ volatile ("mrs %0, psp" : "=r"(psp));

  memset(regs, 0, sizeof(regs));
  if (psp >= base && psp + 8 * sizeof(uint32_t) <= top)
    {
      frame = (FAR uint32_t *)(uintptr_t)psp;
      regs[REG_R0]   = frame[0];
      regs[REG_R1]   = frame[1];
      regs[REG_R2]   = frame[2];
      regs[REG_R3]   = frame[3];
      regs[REG_R12]  = frame[4];
      regs[REG_LR]   = frame[5];
      regs[REG_PC]   = frame[6];
      regs[REG_XPSR] = frame[7];

      /* The walk starts at the frame, so the interrupted PC/LR are in it. */

      regs[REG_SP] = psp;
    }
  else
    {
      /* PSP is not on this task's stack: start at the top, scan nothing. */

      regs[REG_SP] = top;
    }

  rtcb->xcp.regs = regs;
}

/* With BOARD_RESET_ON_ASSERT an AP panic ends in its own board_reset(),
 * i.e. SYSRESETREQ of CPU1 alone, unless the CP was flagged to reset the
 * whole chip.  CPU1 restarting under a running CP races the CP's RPTUN
 * rebuild (rpmsg_virtio.c:900 assert), so require the peer flag. */

#if defined(CONFIG_BK7258_COMPONENT_AP) && \
    defined(CONFIG_BOARD_RESET_ON_ASSERT) && \
    CONFIG_BOARD_RESET_ON_ASSERT >= 1 && \
    !defined(CONFIG_BK7258_RPTUN_PANIC_PEER)
#  error "AP BOARD_RESET_ON_ASSERT needs BK7258_RPTUN_PANIC_PEER"
#endif

#ifdef CONFIG_BK7258_COMPONENT_CP
int bk7258_ap_start_monitor(void);
#ifdef CONFIG_BK7258_WIFI_ARMINO_BASELINE
int bk7258_armino_start(void);
int bk_wdt_stop(void);
int bk_aon_wdt_stop(void);
int bk_wdt_feed(void);
int bk_aon_wdt_feed(void);
#endif

static int bk7258_cp_panic_notify(struct notifier_block *block,
                                  unsigned long action, void *data)
{
  (void)block;
  (void)data;

  if (action == PANIC_KERNEL)
    {
      bk7258_panic_fix_irq_regs();

#ifdef CONFIG_BK7258_WIFI_ARMINO_BASELINE
      /* Armino starts its interrupt watchdog (CONFIG_INT_WDT_PERIOD_MS,
       * 8 s) and AON watchdog (10 s) and feeds them from the tick, which a
       * panic stops.  If the board halts after the dump
       * (BOARD_RESET_ON_ASSERT == 0), stop both so the dump stays on the
       * console.  If it resets (>= 1), feed both once instead: the two
       * dumps take about 2 s, and a dump that hangs is still reset. */

#  if defined(CONFIG_BOARD_RESET_ON_ASSERT) && \
      CONFIG_BOARD_RESET_ON_ASSERT >= 1
      bk_wdt_feed();
      bk_aon_wdt_feed();
#  else
      bk_wdt_stop();
      bk_aon_wdt_stop();
#  endif
#endif
    }

  return 0;
}

static struct notifier_block g_bk7258_cp_panic_notifier =
{
  .notifier_call = bk7258_cp_panic_notify,
  .priority = 100,
};

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

  /* The hardware TRNG.  /dev/urandom is registered by devurandom_register()
   * (chips/bk7258/bk7258_trng.c) from nuttx's drivers_initialize(), under
   * CONFIG_DEV_URANDOM_ARCH — earlier than this point.  What is left here is
   * the Armino-facing half: bk_rand()/bk_fill_rand() and the bk_cli tools
   * expect bk_trng_driver_init() to have run before they use start/stop.
   */

  bk_trng_driver_init();

  /* Root CA for the ai_agent's TLS: pulls in chips/bk7258/bk7258_ca.c. */
  bk7258_ca_init();

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
  panic_notifier_chain_register(&g_bk7258_cp_panic_notifier);
#ifdef CONFIG_RPTUN
  int ret = bk7258_rptun_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[AMP] CP RPTUN init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "[AMP] CP RPTUN master initialized (Mailbox IRQ)\n");
#ifdef CONFIG_BK7258_MB_IPC_RPMSG
      ret = bk7258_mb_ipc_initialize();
      if (ret != 0)
        {
          syslog(LOG_ERR, "[AMP] CP mailbox IPC init failed: %d\n", ret);
        }
#endif
      /* AP-side apps drive Wi-Fi through here (scan / connect /
       * disconnect) rather than over usrsock.
       */
      ret = wlan_ctrl_server_init();
      if (ret != 0)
        {
          syslog(LOG_ERR, "[WLAN-CTRL] init failed: %d\n", ret);
        }
    }

  up_putc('M');
#else
  (void)bk7258_ap_start_monitor();
#endif
#ifdef CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  bk7258_wifi_boot_initialize();
#endif
#ifdef CONFIG_BK7258_WIFI_ARMINO_BASELINE
  (void)bk7258_armino_start();
#endif
#else
  bk7258_ap_initialize();
#endif
}

int board_app_initialize(uintptr_t arg)
{
  static bool initialized;

  /* The CP init task runs this before handing the console to the AP, and
   * nsh_main runs it again (CONFIG_NSH_ARCHINIT) when cu exits. */

  if (initialized)
    {
      return 0;
    }

  initialized = true;

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

  /* The pseudo-fs root accepts directories but not regular files
   * (CONFIG_PSEUDOFS_FILE is off), so /tmp has to be a real filesystem
   * before anything can create a file in it.  Without this every
   * open(..., O_CREAT) under /tmp fails with ENOENT and looks like the
   * directory is missing.
   */

  if (mount("tmpfs", "/tmp", "tmpfs", 0, NULL) < 0)
    {
      syslog(LOG_ERR, "[BK7258] tmpfs mount failed: %d\n", get_errno());
    }

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

/* GPIO40 now belongs to bk7258_status_led.c, which drives it as the red
 * half of the two-colour status indicator.  This heartbeat writer kept
 * toggling the same pin once a second and its writes landed between the
 * status writer's, which showed up as a red flicker during the green idle
 * blink.  The liveness proof the heartbeat existed for is carried by the
 * status LED's idle pattern, so the writer is retired; the call sites stay
 * so the surrounding sequencing is unchanged.
 */

static void bk7258_ap_heartbeat_led_write(bool on)
{
  (void)on;
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

/* Copy the panic essentials into SWAP for the CP monitor: the AP console is
 * an RPMsg UART and cannot carry NuttX's own dump once interrupts are off. */

static void bk7258_ap_panic_capture(FAR struct panic_notifier_s *info)
{
  static bool captured;
  struct bk7258_ap_panic_s panic;
  FAR uint32_t *regs;
  size_t len;

  if (captured || info == NULL)
    {
      return;
    }

  captured = true;
  memset(&panic, 0, sizeof(panic));
  panic.linenum = (uint32_t)info->linenum;

  /* Keep the tail of a long path: the file name is the useful part. */

  if (info->filename != NULL)
    {
      len = strlen(info->filename);
      strlcpy(panic.filename,
              len >= sizeof(panic.filename) ?
              info->filename + len - (sizeof(panic.filename) - 1) :
              info->filename,
              sizeof(panic.filename));
    }

  if (info->msg != NULL)
    {
      strlcpy(panic.msg, info->msg, sizeof(panic.msg));
    }

#if CONFIG_TASK_NAME_SIZE > 0
  if (info->rtcb != NULL)
    {
      strlcpy(panic.task, info->rtcb->name, sizeof(panic.task));
    }
#endif

  regs = info->regs;
  if (regs != NULL)
    {
      panic.pc = regs[REG_PC];
      panic.lr = regs[REG_LR];
      panic.sp = regs[REG_SP];
    }

  /* Publish before unwinding: a fault inside up_backtrace() must not cost
   * the location that is already known. */

  bk7258_ap_panic_write(&panic);

  /* Inside an interrupt (the RPTUN peer-panic kick from the CP) the unwinder
   * took a precise BusFault on the board; keep the record without frames. */

  if (up_interrupt_context())
    {
      return;
    }

#ifdef CONFIG_ARCH_HAVE_BACKTRACE
  {
    FAR void *bt[BK7258_AP_PANIC_NBT];
    int n = up_backtrace(info->rtcb, bt, BK7258_AP_PANIC_NBT, 0);
    int i;

    for (i = 0; i < n && i < BK7258_AP_PANIC_NBT; i++)
      {
        panic.bt[i] = (uint32_t)(uintptr_t)bt[i];
      }

    panic.nbt = i;
    bk7258_ap_panic_write(&panic);
  }
#endif
}

/* Panic-time syslog channel: the RPMsg console is dead once interrupts are
 * off, so the dump goes straight to the UART0 FIFO (up_putc, token held). */

static int bk7258_ap_panic_putc(FAR syslog_channel_t *channel, int ch)
{
  (void)channel;
  up_putc(ch);
  return ch;
}

static ssize_t bk7258_ap_panic_puts(FAR syslog_channel_t *channel,
                                    FAR const char *buf, size_t len)
{
  size_t i;

  (void)channel;
  for (i = 0; i < len; i++)
    {
      up_putc(buf[i]);
    }

  return len;
}

static const struct syslog_channel_ops_s g_bk7258_ap_panic_ops =
{
  bk7258_ap_panic_putc,
  bk7258_ap_panic_putc,
  NULL,
  bk7258_ap_panic_puts,
  bk7258_ap_panic_puts,
  NULL
};

static syslog_channel_t g_bk7258_ap_panic_channel =
{
  .sc_ops = &g_bk7258_ap_panic_ops,
};

static int bk7258_ap_panic_notify(struct notifier_block *block,
                                  unsigned long action, void *data)
{
  FAR struct panic_notifier_s *info = data;
  uint32_t fault;

  (void)block;

  /* PANIC_TASK can describe a task-local assertion. Only kernel panic stages
   * are fatal evidence for this AP instance. */

  if (action == PANIC_KERNEL_FINAL)
    {
      /* The dump is out.  Hand UART0 back before _assert() runs the reboot
       * notifier chain, where RPTUN (RPTUN_AUTO_RESET_IN_REBOOT_NOTIFIER)
       * wakes the CP to dump its own state. */

      /* An interrupt-context dump goes through SYSLOG_INTBUFFER; drain its
       * tail to UART0 while the token is still ours. */

      syslog_flush();
      bk7258_ap_uart_token_release();
      return 0;
    }

  if (action != PANIC_TASK)
    {
      bk7258_panic_fix_irq_regs();

      /* Mute the CP and route the coming NuttX dump to UART0.  The single
       * syslog slot is replaced, not shared (SYSLOG_MAX_CHANNELS=1). */

      bk7258_ap_uart_token_take();
      syslog_channel_register(&g_bk7258_ap_panic_channel);

      /* Before the FAULT stage, so the CP never sees a fault without it. */

      bk7258_ap_panic_capture(info);

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

  /* v5c probe: entry mark.  With the pin removed, a terminal word-40 value
   * of 0x0327 proves the full v4j-style natural dispatch: amp-init ran on
   * the secondary core and completed rptun + mb_ipc + scheduler-running.
   * Terminal 0x0324 = rptun failed, 0x0326 = mb_ipc failed fast (branch),
   * 0x0323 = rptun ok but mb_ipc never returned (hung). */

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AMP_ENTRY);

  ret = bk7258_rptun_initialize();
  if (ret < 0)
    {
      bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AMP_RPTUN_FAIL);

      /* Keep the heartbeat task alive so the SWAP record still proves AP
       * liveness even when AMP initialization fails.
       */

      return ret;
    }

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AMP_RPTUN_OK);

#ifdef CONFIG_BK7258_MB_IPC_RPMSG
  ret = bk7258_mb_ipc_initialize();
  if (ret != 0)
    {
      bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AMP_MBIPC_FAIL);
      return ret;
    }

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AMP_MBIPC_OK);
#endif

  /* No rpmsg netdev registration here: the NET_RPMSG_DRV client/server
   * pair deadlocks on this platform (the AP's rpmsg_create_ept sends an
   * NS announcement and waits synchronously; the CP's ns_bind handler
   * answers it by creating the peer endpoint, whose own announcement
   * waits for an ACK the blocked AP will never send).  Cross-core
   * networking goes through the Vela-standard NET_RPMSG socket domain
   * instead; the transport this kthread initialized above is shared. */

  /* Publish scheduler-running only after the AP-side RPTUN/RPMsg instance is
   * initialized.  CP uses the SWAP generation transition as the trigger to
   * tear down and rebuild its stale remote transport, so advertising this
   * stage earlier would let CP restart AP while its new RPTUN instance was
   * still coming up. */

  bk7258_ap_record_mark_scheduler();

  /* v5c probe: scheduler-running record written.  Terminal 0x0327 closes
   * the amp-init success chain proved by this image's own writes. */

  bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AMP_STAGE_MARK);

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

/* v5c: no pinning.  The amp-init/ap-health kthreads are dispatched by the
 * SMP scheduler like every other task; the gate flush (below) re-delivers
 * the kick the gate window swallowed, so they strand briefly on CPU1 and
 * then run there the way v4j ran them -- the healthy baseline for RPTUN +
 * mb_ipc + heartbeat.
 */

/* v5c gate-flush poll budget.  CPU2 writes its READY mark (Mailbox + SysTick
 * armed) within microseconds-to-milliseconds of the gate opening; the bound
 * only prevents CPU0 from spinning forever if CPU2 dies.  Sized a quarter of
 * the CPU2 gate-wait budget so a normal boot exits in a handful of polls. */

#define BK7258_AP_FLUSH_LOOPS   UINT32_C(5000000)

static void bk7258_ap_initialize(void)
{
  int ret;

  /* board_late_initialize() runs before nsh_main. Keep board health and AMP
   * setup in kernel workers so the standard NSH entry can own /dev/console.
   * The notifier is registered here, after tasklist_initialize(): earlier
   * registration would dereference a NULL TCB inside sched_lock().
   */

  syslog(LOG_INFO, "BK7258: bk7258_ap_initialize called\n");

#ifdef CONFIG_BK7258_PSRAM
  /* The .psram_bss section (flash_ap.ld) is NOLOAD: startup zeroes only
   * _sbss.._ebss.  PSRAM is powered by now -- the CP ran pm_hardware_init()
   * before it released this core -- so clear the range here, before any of
   * the ai_agent or UI code touches those statics.
   */

  {
    extern uint8_t _spsram_bss[];
    extern uint8_t _epsram_bss[];

    memset(_spsram_bss, 0, (size_t)(_epsram_bss - _spsram_bss));
    syslog(LOG_INFO, "BK7258: cleared %zu bytes of .psram bss\n",
           (size_t)(_epsram_bss - _spsram_bss));
  }
#endif

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
  if (ret == 0)
    {
      /* LED first: the key thread steps it, so the pins must be configured
       * before that thread exists.
       */

      bk7258_status_led_initialize();
      bk7258_buttons_input_initialize();
    }
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

  /* The heartbeat is the only thing the CP's stall detector sees, so it
   * must not starve behind application threads.  At 80 it sat below
   * ai_agent's LVGL loop and lvgldemo (100), and a busy render loop alone
   * produced "[AP] stalled".  200 stays below rpmsg (224) and AppBringUp
   * (240); the loop only sleeps 1 s, toggles the LED and bumps a counter,
   * so a stall now means the AP really stopped scheduling.
   */

  ret = kthread_create("bk7258-ap-health", 200, 2048,
                       bk7258_ap_health_thread, NULL);
  if (ret < 0)
    {
      bk7258_ap_record_fault(UINT32_C(0x48425400) |
                             ((uint32_t)-ret & UINT32_C(0xff)));
      bk7258_ap_heartbeat_led_write(false);
    }

  /* CPU1 scheduler gate (v5): every early-board heap object this initializer
   * just built (audio device state, the amp-init/ap-health kthread TCBs)
   * is now fully constructed under single-core scheduling.  Opening the
   * gate lets CPU2, which has been spinning in bk7258_cpu2_boot() with its
   * Mailbox IPI and SysTick still disarmed, arm both and join the shared
   * scheduler.  The v4j sched-trace showed both cores climbing these exact
   * objects (0x280358d8 first-switch / amp-init 0x28036490 / ap-health
   * 0x28038580) concurrently a few milliseconds after the release; from
   * here on CPU2's first scheduling decisions happen strictly after CPU0
   * finished constructing its threads. */

  bk7258_ap_sched_gate_set();

  /* v5c gate flush: CPU2 wrote READY (word 40 == 0x0303) only after arming
   * its Mailbox IPI and SysTick.  The amp-init/ap-health kthreads created
   * above were dispatched to CPU2 while it was still spinning in the gate,
   * so their kick landed in the unarmed Mailbox and was lost -- they sit in
   * g_delivertasks[1] until an SMP kick runs nxsched_process_delivered().
   * Re-send the kick now that CPU2 can receive it, restoring the natural
   * dispatch (CPU2 executes the AMP threads) without the v5b pin hack.
   * Bounded spin: on timeout we degrade to the pre-fix behavior rather than
   * hang the *primary* core; the monitor still gets a discriminating
   * flush-timeout mark. */

  {
    uint32_t flush_count;

    for (flush_count = 0; flush_count < BK7258_AP_FLUSH_LOOPS; flush_count++)
      {
        if (bk7258_ap_dbg_read() == BK7258_AP_DBG_MARK_CPU2_READY)
          {
            break;
          }
      }

    (void)up_send_smp_sched(1);

    if (flush_count < BK7258_AP_FLUSH_LOOPS)
      {
        bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AP_FLUSH_DONE);
      }
    else
      {
        syslog(LOG_WARNING,
               "BK7258: gate flush timed out (CPU2 READY never seen)\n");
        bk7258_ap_dbg_mark(BK7258_AP_DBG_MARK_AP_FLUSH_TIMEOUT);
      }
  }
}
#endif

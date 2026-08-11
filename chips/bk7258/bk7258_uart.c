/****************************************************************************
 * chips/bk7258/bk7258_uart.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/serial/serial.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/irq.h"

struct bk7258_uart_s
{
  uint32_t ie;
};

static char g_rxbuffer[CONFIG_BK7258_UART0_RXBUFSIZE];
static char g_txbuffer[CONFIG_BK7258_UART0_TXBUFSIZE];
static struct bk7258_uart_s g_uartpriv;

static int bk7258_uart_setup(struct uart_dev_s *dev);
static void bk7258_uart_shutdown(struct uart_dev_s *dev);
static int bk7258_uart_attach(struct uart_dev_s *dev);
static void bk7258_uart_detach(struct uart_dev_s *dev);
static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg);
static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status);
static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_rxavailable(struct uart_dev_s *dev);
static void bk7258_uart_send(struct uart_dev_s *dev, int ch);
static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_txready(struct uart_dev_s *dev);
static bool bk7258_uart_txempty(struct uart_dev_s *dev);
static int bk7258_uart_interrupt(int irq, void *context, void *arg);

static const struct uart_ops_s g_uart_ops =
{
  .setup       = bk7258_uart_setup,
  .shutdown    = bk7258_uart_shutdown,
  .attach      = bk7258_uart_attach,
  .detach      = bk7258_uart_detach,
  .ioctl       = bk7258_uart_ioctl,
  .receive     = bk7258_uart_receive,
  .rxint       = bk7258_uart_rxint,
  .rxavailable = bk7258_uart_rxavailable,
  .send        = bk7258_uart_send,
  .txint       = bk7258_uart_txint,
  .txready     = bk7258_uart_txready,
  .txempty     = bk7258_uart_txempty,
};

static struct uart_dev_s g_uartport =
{
  .isconsole = true,
  .recv =
  {
    .size = CONFIG_BK7258_UART0_RXBUFSIZE,
    .buffer = g_rxbuffer,
  },
  .xmit =
  {
    .size = CONFIG_BK7258_UART0_TXBUFSIZE,
    .buffer = g_txbuffer,
  },
  .ops = &g_uart_ops,
  .priv = &g_uartpriv,
};

static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  (void)dev;
  bk7258_lowsetup();
  bk7258_gpio_uart0_rx();
  modifyreg32(BK7258_UART_CONFIG, 0, BK7258_UART_CONFIG_RX_EN);
  return OK;
}

static void bk7258_uart_shutdown(struct uart_dev_s *dev)
{
  (void)dev;
  putreg32(0, BK7258_UART_INT_ENABLE);
}

static int bk7258_uart_attach(struct uart_dev_s *dev)
{
  int ret = irq_attach(BK7258_IRQ_UART0, bk7258_uart_interrupt, dev);
  if (ret == OK)
    {
      modifyreg32(BK7258_SYS_CPU0_INT_EN, 0, BK7258_SYS_UART0_INT_EN);
      up_enable_irq(BK7258_IRQ_UART0);
    }

  return ret;
}

static void bk7258_uart_detach(struct uart_dev_s *dev)
{
  (void)dev;
  putreg32(0, BK7258_UART_INT_ENABLE);
  up_disable_irq(BK7258_IRQ_UART0);
  modifyreg32(BK7258_SYS_CPU0_INT_EN, BK7258_SYS_UART0_INT_EN, 0);
  irq_detach(BK7258_IRQ_UART0);
}

static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  uint32_t value;

  (void)dev;
  value = getreg32(BK7258_UART_FIFO_PORT);
  *status = 0;
  return (value >> 8) & 0xff;
}

static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      priv->ie |= BK7258_UART_INT_RX_READY | BK7258_UART_INT_RX_FINISH;
    }
  else
    {
      priv->ie &= ~(BK7258_UART_INT_RX_READY | BK7258_UART_INT_RX_FINISH);
    }

  putreg32(priv->ie, BK7258_UART_INT_ENABLE);
}

static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  (void)dev;
  return (getreg32(BK7258_UART_FIFO_STATUS) & BK7258_UART_FIFO_RD_READY) != 0;
}

static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  (void)dev;
  bk7258_lowputc((char)ch);
}

static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      /* Start the transfer immediately, then leave the hardware FIFO-ready
       * interrupt enabled so that the ISR drains any bytes which did not fit
       * in the FIFO on this first pass. */

      priv->ie |= BK7258_UART_INT_TX_READY;
      putreg32(priv->ie, BK7258_UART_INT_ENABLE);
      uart_xmitchars(dev);
    }
  else
    {
      priv->ie &= ~BK7258_UART_INT_TX_READY;
      putreg32(priv->ie, BK7258_UART_INT_ENABLE);
    }
}

static bool bk7258_uart_txready(struct uart_dev_s *dev)
{
  (void)dev;
  return (getreg32(BK7258_UART_FIFO_STATUS) & BK7258_UART_FIFO_WR_READY) != 0;
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  (void)dev;
  return (getreg32(BK7258_UART_FIFO_STATUS) & (UINT32_C(1) << 17)) != 0;
}

static int bk7258_uart_interrupt(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = arg;
  uint32_t status;

  (void)irq;
  (void)context;
  status = getreg32(BK7258_UART_INT_STATUS) & 0xff;
  putreg32(status, BK7258_UART_INT_STATUS);

  if (status & (BK7258_UART_INT_RX_READY | BK7258_UART_INT_RX_FINISH))
    {
      uart_recvchars(dev);
    }

  if (status & BK7258_UART_INT_TX_READY)
    {
      uart_xmitchars(dev);
    }

  return OK;
}

void arm_earlyserialinit(void)
{
  bk7258_lowsetup();
}

void arm_serialinit(void)
{
  /* The serial core treats a console as pre-initialized and skips setup() on
   * its first open. Complete RX pinmux and UART receive enable before
   * registering the console so NSH can receive input as well as emit early
   * polling output. */

  (void)bk7258_uart_setup(&g_uartport);

  uart_register("/dev/console", &g_uartport);
  uart_register("/dev/ttyS0", &g_uartport);
}

void up_putc(int ch)
{
  bk7258_lowputc((char)ch);
}

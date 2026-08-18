/****************************************************************************
 * board/bk7258-devkit/src/bk7258_rptun.c
 *
 * A2-2 CP/AP RPTUN transport. RPMsg data stays in shared SRAM and BK7258
 * Mailbox v2 provides the hardware doorbell in both directions.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RPTUN

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/irq.h>
#include <nuttx/nuttx.h>
#include <nuttx/rptun/rptun.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_mailbox.h>
#include <arch/chip/bk7258_memorymap.h>

#include "bk7258_rptun.h"

#define BK7258_RPTUN_VRINGS        2
#define BK7258_RPTUN_VRING_ALIGN   8
#define BK7258_RPTUN_VRING_NUM     8
#define BK7258_RPTUN_BUFFER_SIZE   512

struct bk7258_rptun_shmem_s
{
  volatile uintptr_t base;
  volatile uint32_t mbox_pending[2];
  struct rptun_rsc_s rsc;
};

struct bk7258_rptun_dev_s
{
  struct rptun_dev_s rptun;
  rptun_callback_t callback;
  void *callback_arg;
  struct bk7258_rptun_shmem_s *shmem;
  bool master;
};

static_assert(sizeof(struct bk7258_rptun_shmem_s) <
              CONFIG_BK7258_RPMSG_SHM_SIZE,
              "BK7258 RPTUN resource header exceeds RPMSG_SHM");
static_assert(sizeof(struct bk7258_rptun_shmem_s) +
              BK7258_RPTUN_BUFFER_SIZE * BK7258_RPTUN_VRING_NUM *
                BK7258_RPTUN_VRINGS + 0x1000 <=
              CONFIG_BK7258_RPMSG_SHM_SIZE,
              "BK7258 RPMSG_SHM cannot hold vrings and RPMsg buffers");

static const char *bk7258_rptun_get_cpuname(struct rptun_dev_s *dev);
static struct resource_table *
bk7258_rptun_get_resource(struct rptun_dev_s *dev);
static bool bk7258_rptun_is_autostart(struct rptun_dev_s *dev);
static bool bk7258_rptun_is_master(struct rptun_dev_s *dev);
static int bk7258_rptun_start(struct rptun_dev_s *dev);
static int bk7258_rptun_stop(struct rptun_dev_s *dev);
static int bk7258_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid);
static int bk7258_rptun_register_callback(struct rptun_dev_s *dev,
                                          rptun_callback_t callback,
                                          void *arg);

static const struct rptun_ops_s g_bk7258_rptun_ops =
{
  .get_cpuname       = bk7258_rptun_get_cpuname,
  .get_resource      = bk7258_rptun_get_resource,
  .is_autostart      = bk7258_rptun_is_autostart,
  .is_master         = bk7258_rptun_is_master,
  .start             = bk7258_rptun_start,
  .stop              = bk7258_rptun_stop,
  .notify            = bk7258_rptun_notify,
  .register_callback = bk7258_rptun_register_callback,
};

static struct bk7258_rptun_dev_s g_bk7258_rptun =
{
  .rptun =
  {
    .ops = &g_bk7258_rptun_ops,
  },
  .shmem = (struct bk7258_rptun_shmem_s *)
             CONFIG_BK7258_RPMSG_SHM_ADDR,
#ifdef CONFIG_BK7258_COMPONENT_CP
  .master = true,
#else
  .master = false,
#endif
};

static inline void bk7258_rptun_barrier(void)
{
  __asm__ volatile ("dmb" : : : "memory");
}

static void bk7258_rptun_enable_shared_sram(void)
{
  volatile uint32_t *power_wakeup =
    (volatile uint32_t *)BK7258_SYS_POWER_WAKEUP;
  uint32_t value = *power_wakeup;

  /* RPMSG_SHM spans the shared SRAM bank below CP RAM. Keep shared-SRAM
   * clock gating disabled while either core can touch the resource table,
   * vrings or RPMsg buffers. SWAP heartbeat alone did not exercise this bank.
   */

  *power_wakeup = value | BK7258_SYS_SHARE_MEM_CLKGATING_DISABLE;
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
  (void)*power_wakeup;
}

static const char *bk7258_rptun_get_cpuname(struct rptun_dev_s *dev)
{
  (void)dev;
#ifdef CONFIG_BK7258_COMPONENT_CP
  return "ap";
#else
  return "cp";
#endif
}

static struct resource_table *
bk7258_rptun_get_resource(struct rptun_dev_s *dev)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);
  struct rptun_rsc_s *rsc = &priv->shmem->rsc;
  uintptr_t carveout_base;
  size_t carveout_size;

  if (priv->master)
    {
      rsc->rsc_tbl_hdr.ver = 1;
      rsc->rsc_tbl_hdr.num = 2;
      rsc->rsc_tbl_hdr.reserved[0] = 0;
      rsc->rsc_tbl_hdr.reserved[1] = 0;
      rsc->offset[0] = offsetof(struct rptun_rsc_s, rpmsg_vdev);
      rsc->offset[1] = offsetof(struct rptun_rsc_s, carveout);

      rsc->rpmsg_vdev.type = RSC_VDEV;
      rsc->rpmsg_vdev.id = VIRTIO_ID_RPMSG;
      rsc->rpmsg_vdev.notifyid = RSC_NOTIFY_ID_ANY;
      rsc->rpmsg_vdev.dfeatures =
        (UINT32_C(1) << VIRTIO_RPMSG_F_NS) |
        (UINT32_C(1) << VIRTIO_RPMSG_F_ACK) |
        (UINT32_C(1) << VIRTIO_RPMSG_F_BUFSZ) |
        (UINT32_C(1) << VIRTIO_RPMSG_F_CPUNAME);
      rsc->rpmsg_vdev.gfeatures = 0;
      rsc->rpmsg_vdev.config_len = sizeof(struct fw_rsc_config);
      rsc->rpmsg_vdev.status = 0;
      rsc->rpmsg_vdev.num_of_vrings = BK7258_RPTUN_VRINGS;
      /* The resource-table host is the VirtIO driver. RPTUN XORs this field
       * with its local master flag, yielding driver on CP and device on AP. */

      rsc->rpmsg_vdev.reserved[0] = VIRTIO_DEV_DRIVER;
      rsc->rpmsg_vdev.reserved[1] = 0;

      rsc->rpmsg_vring0.da = FW_RSC_U32_ADDR_ANY;
      rsc->rpmsg_vring0.align = BK7258_RPTUN_VRING_ALIGN;
      rsc->rpmsg_vring0.num = BK7258_RPTUN_VRING_NUM;
      rsc->rpmsg_vring0.notifyid = RSC_NOTIFY_ID_ANY;
      rsc->rpmsg_vring0.reserved = 0;
      rsc->rpmsg_vring1.da = FW_RSC_U32_ADDR_ANY;
      rsc->rpmsg_vring1.align = BK7258_RPTUN_VRING_ALIGN;
      rsc->rpmsg_vring1.num = BK7258_RPTUN_VRING_NUM;
      rsc->rpmsg_vring1.notifyid = RSC_NOTIFY_ID_ANY;
      rsc->rpmsg_vring1.reserved = 0;

      memset(&rsc->config, 0, sizeof(rsc->config));
      rsc->config.h2r_buf_size = BK7258_RPTUN_BUFFER_SIZE;
      rsc->config.r2h_buf_size = BK7258_RPTUN_BUFFER_SIZE;
      strlcpy((char *)rsc->config.host_cpuname, "cp",
              sizeof(rsc->config.host_cpuname));
      strlcpy((char *)rsc->config.remote_cpuname, "ap",
              sizeof(rsc->config.remote_cpuname));

      carveout_base = ALIGN_UP((uintptr_t)rsc + sizeof(*rsc),
                               BK7258_RPTUN_VRING_ALIGN);
      carveout_size = CONFIG_BK7258_RPMSG_SHM_ADDR +
                      CONFIG_BK7258_RPMSG_SHM_SIZE - carveout_base;
      rsc->carveout.type = RSC_CARVEOUT;
      rsc->carveout.da = carveout_base;
      rsc->carveout.pa = FW_RSC_U32_ADDR_ANY;
      rsc->carveout.len = carveout_size;
      rsc->carveout.flags = 0;
      rsc->carveout.reserved = 0;
      strlcpy((char *)rsc->carveout.name, "rpmsg_shm",
              sizeof(rsc->carveout.name));

      bk7258_rptun_barrier();
      priv->shmem->base = CONFIG_BK7258_RPMSG_SHM_ADDR;
      bk7258_rptun_barrier();
    }
  else
    {
      while (priv->shmem->base != CONFIG_BK7258_RPMSG_SHM_ADDR)
        {
          nxsig_usleep(100);
        }

      bk7258_rptun_barrier();
    }

  return &rsc->rsc_tbl_hdr;
}

static bool bk7258_rptun_is_autostart(struct rptun_dev_s *dev)
{
  (void)dev;
  return false;
}

static bool bk7258_rptun_is_master(struct rptun_dev_s *dev)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);
  return priv->master;
}

static int bk7258_rptun_start(struct rptun_dev_s *dev)
{
  (void)dev;
#ifdef CONFIG_BK7258_COMPONENT_CP
  extern int bk7258_ap_start_monitor(void);
  return bk7258_ap_start_monitor();
#else
  return 0;
#endif
}

static int bk7258_rptun_stop(struct rptun_dev_s *dev)
{
  (void)dev;
  return -ENOTSUP;
}

static int bk7258_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);
  int peer = priv->master ? 1 : 0;
  irqstate_t flags;
  int ret;

  (void)vqid;
  bk7258_rptun_barrier();
  /* Every Mailbox entry is an idempotent request to scan all virtqueues.
   * Keep at most one doorbell outstanding in each direction.  This explicit
   * latch, rather than the instantaneous FIFO-full state, makes coalescing
   * race-free: the receiver clears it before scanning the virtqueues.
   */

  flags = enter_critical_section();
  if (priv->shmem->mbox_pending[peer] != 0)
    {
      leave_critical_section(flags);
      return 0;
    }

  priv->shmem->mbox_pending[peer] = 1;
  bk7258_rptun_barrier();
  ret = bk7258_mbox_notify(peer, RPTUN_NOTIFY_ALL);
  if (ret == -EBUSY)
    {
      /* One outstanding RPTUN doorbell cannot fill its dedicated FIFO.
       * Treat this as an ownership/hardware invariant failure instead of
       * hiding it behind a timer or an assumed notification.
       */

      priv->shmem->mbox_pending[peer] = 0;
      bk7258_rptun_barrier();
      leave_critical_section(flags);
      return ret;
    }

  if (ret < 0)
    {
      priv->shmem->mbox_pending[peer] = 0;
      bk7258_rptun_barrier();
    }

  leave_critical_section(flags);
  return ret;
}

static int bk7258_rptun_register_callback(struct rptun_dev_s *dev,
                                          rptun_callback_t callback,
                                          void *arg)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);
  irqstate_t flags;

  flags = enter_critical_section();
  priv->callback = callback;
  priv->callback_arg = arg;
  leave_critical_section(flags);

  if (callback != NULL)
    {
      /* Match the established NuttX nRF53/STM32 RPTUN pattern: scan once
       * after callback registration to cover peer writes that predate local
       * IRQ setup.  All subsequent notifications are hardware-driven.
       */

      callback(arg, RPTUN_NOTIFY_ALL);
    }

  return 0;
}

static void bk7258_rptun_mbox_callback(uint8_t src_cpu, uint32_t data0,
                                       uint32_t data1, void *arg)
{
  struct bk7258_rptun_dev_s *priv = arg;
  unsigned int local = priv->master ? 0 : 1;
  uint8_t peer = priv->master ? 1 : 0;

  (void)data0;
  if (src_cpu != peer || data1 != BK7258_MBOX_RPTUN_MAGIC)
    {
      return;
    }

  /* Re-arm the direction before scanning.  A concurrent sender either
   * coalesced before this clear (and is covered by this scan), or observes
   * zero and emits a new hardware doorbell.
   */

  priv->shmem->mbox_pending[local] = 0;
  bk7258_rptun_barrier();
  if (priv->callback != NULL)
    {
      priv->callback(priv->callback_arg, RPTUN_NOTIFY_ALL);
    }
}

int bk7258_rptun_initialize(void)
{
  unsigned int local = g_bk7258_rptun.master ? 0 : 1;
  int ret;

  bk7258_rptun_enable_shared_sram();

  if (g_bk7258_rptun.master)
    {
      memset(g_bk7258_rptun.shmem, 0, CONFIG_BK7258_RPMSG_SHM_SIZE);
      bk7258_rptun_barrier();
    }

  ret = bk7258_mbox_attach(bk7258_rptun_mbox_callback,
                           &g_bk7258_rptun);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_mbox_init(g_bk7258_rptun.master);
  if (ret < 0)
    {
      return ret;
    }

  /* Mailbox init has discarded entries left by an earlier boot.  Re-arm the
   * matching shared latch only after that drain, so a restarted core cannot
   * leave its peer permanently coalescing against stale state.
   */

  g_bk7258_rptun.shmem->mbox_pending[local] = 0;
  bk7258_rptun_barrier();

  ret = rptun_initialize(&g_bk7258_rptun.rptun);
  if (ret < 0)
    {
      return ret;
    }
  ret = rptun_boot(bk7258_rptun_get_cpuname(&g_bk7258_rptun.rptun));
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

#endif /* CONFIG_RPTUN */

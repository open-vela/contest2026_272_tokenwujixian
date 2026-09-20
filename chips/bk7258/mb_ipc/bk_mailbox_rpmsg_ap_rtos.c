/****************************************************************************
 * AP-local NuttX RTOS primitives required by imported Armino mailbox IPC.
 *
 * CP obtains these symbols from the existing Wi-Fi OSAL. AP does not compile
 * that OSAL, so this file maps only the authority mailbox state machine's
 * semaphore, interrupt-mask, and monotonic-time contract onto NuttX.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_MB_IPC_RPMSG) && \
    defined(CONFIG_BK7258_COMPONENT_AP)

#include <errno.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include <common/bk_err.h>
#include <os/os.h>

struct bk7258_mb_ipc_sem_s
{
  sem_t sem;
  spinlock_t lock;
  unsigned int max_count;
};

bk_err_t rtos_init_semaphore(beken_semaphore_t *sem, int max_count)
{
  struct bk7258_mb_ipc_sem_s *wrapper;

  if (sem == NULL || max_count <= 0)
    {
      return BK_ERR_PARAM;
    }

  wrapper = kmm_zalloc(sizeof(*wrapper));
  if (wrapper == NULL)
    {
      return BK_ERR_NO_MEM;
    }

  if (nxsem_init(&wrapper->sem, 0, 0) < 0)
    {
      kmm_free(wrapper);
      return BK_FAIL;
    }

  spin_lock_init(&wrapper->lock);
  wrapper->max_count = (unsigned int)max_count;
  *sem = wrapper;
  return BK_OK;
}

bk_err_t rtos_deinit_semaphore(beken_semaphore_t *sem)
{
  struct bk7258_mb_ipc_sem_s *wrapper;

  if (sem == NULL || *sem == NULL)
    {
      return BK_ERR_PARAM;
    }

  wrapper = *sem;
  if (nxsem_destroy(&wrapper->sem) < 0)
    {
      return BK_FAIL;
    }

  kmm_free(wrapper);
  *sem = NULL;
  return BK_OK;
}

bk_err_t rtos_get_semaphore(beken_semaphore_t *sem, uint32_t timeout_ms)
{
  struct bk7258_mb_ipc_sem_s *wrapper;
  int ret;

  if (sem == NULL || *sem == NULL || up_interrupt_context())
    {
      return BK_FAIL;
    }

  wrapper = *sem;
  if (timeout_ms == 0)
    {
      ret = nxsem_trywait(&wrapper->sem);
    }
  else if (timeout_ms == UINT32_MAX)
    {
      ret = nxsem_wait_uninterruptible(&wrapper->sem);
    }
  else
    {
      ret = nxsem_tickwait_uninterruptible(&wrapper->sem,
                                           MSEC2TICK(timeout_ms));
    }

  return ret < 0 ? BK_FAIL : BK_OK;
}

bk_err_t rtos_set_semaphore(beken_semaphore_t *sem)
{
  struct bk7258_mb_ipc_sem_s *wrapper;
  irqstate_t flags;
  int value;
  int ret;

  if (sem == NULL || *sem == NULL)
    {
      return BK_ERR_PARAM;
    }

  wrapper = *sem;
  flags = spin_lock_irqsave(&wrapper->lock);
  ret = nxsem_get_value(&wrapper->sem, &value);
  if (ret == 0 && value >= 0 && (unsigned int)value >= wrapper->max_count)
    {
      spin_unlock_irqrestore(&wrapper->lock, flags);
      return BK_ERR_BUSY;
    }

  if (ret == 0)
    {
      ret = nxsem_post(&wrapper->sem);
    }

  spin_unlock_irqrestore(&wrapper->lock, flags);
  return ret < 0 ? BK_FAIL : BK_OK;
}

uint32_t rtos_disable_int(void)
{
  return up_irq_save();
}

void rtos_enable_int(uint32_t level)
{
  up_irq_restore(level);
}

uint32_t rtos_get_time(void)
{
  return TICK2MSEC(clock_systime_ticks());
}

#endif /* CONFIG_BK7258_MB_IPC_RPMSG && CONFIG_BK7258_COMPONENT_AP */

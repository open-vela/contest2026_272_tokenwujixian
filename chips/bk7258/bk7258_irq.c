/****************************************************************************
 * chips/bk7258/bk7258_irq.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_internal.h"
#include "include/bk7258_irq.h"
#include "include/bk7258_memorymap.h"

extern const void *const _vectors[];

static int bk7258_icu_irq(unsigned int source)
{
  return source < 64 ? NVIC_IRQ_FIRST + (int)source : -EINVAL;
}

static int bk7258_icu_set(unsigned int source, bool enable)
{
  irqstate_t flags;
  uintptr_t reg;
  uint32_t mask;
  int ret;

  if (source < 32)
    {
      reg = BK7258_SYS_CPU0_INT_EN;
      mask = BK7258_SYS_IRQ_GROUP0(source);
    }
  else if (source < 64)
    {
      reg = BK7258_SYS_CPU0_INT_EN_HI;
      mask = BK7258_SYS_IRQ_GROUP1(source);
    }
  else
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  modifyreg32(reg, mask, enable ? mask : 0);
  ret = ((getreg32(reg) & mask) != 0) == enable ? OK : -EIO;
  leave_critical_section(flags);
  return ret;
}

int bk7258_icu_attach(unsigned int source, xcpt_t handler, void *arg)
{
  int irq = bk7258_icu_irq(source);
  int ret = irq < 0 || handler == NULL ? -EINVAL : irq_attach(irq, handler, arg);
  syslog(LOG_INFO, "[BK7258] ICU attach source=%u irq=%d ret=%d\n",
         source, irq, ret);
  return ret;
}

int bk7258_icu_enable(unsigned int source)
{
  int irq = bk7258_icu_irq(source);
  int ret;

  if (irq < 0)
    {
      return irq;
    }

  ret = bk7258_icu_set(source, true);
  if (ret < 0)
    {
      return ret;
    }

  up_enable_irq(irq);
  syslog(LOG_INFO, "[BK7258] ICU enable source=%u irq=%d ret=%d int_en=0x%08lx\n",
         source, irq, ret, (unsigned long)getreg32(BK7258_SYS_CPU0_INT_EN));
  return OK;
}

int bk7258_icu_disable(unsigned int source)
{
  int irq = bk7258_icu_irq(source);
  int ret;

  if (irq < 0)
    {
      return irq;
    }

  up_disable_irq(irq);
  ret = bk7258_icu_set(source, false);
  syslog(LOG_INFO, "[BK7258] ICU disable source=%u irq=%d ret=%d int_en=0x%08lx\n",
         source, irq, ret, (unsigned long)getreg32(BK7258_SYS_CPU0_INT_EN));
  return ret;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_irqinfo(int irq, uintptr_t *regaddr, uint32_t *bit,
                          uintptr_t offset)
{
  int external;

  if (irq >= NVIC_IRQ_FIRST && irq < NR_IRQS)
    {
      external = irq - NVIC_IRQ_FIRST;
      *regaddr = NVIC_IRQ_ENABLE(external) + offset;
      *bit = UINT32_C(1) << (external & 31);
      return OK;
    }

  if (irq == NVIC_IRQ_SYSTICK)
    {
      *regaddr = NVIC_SYSTICK_CTRL;
      *bit = NVIC_SYSTICK_CTRL_ENABLE;
      return OK;
    }

  return -EINVAL;
}

int up_prioritize_irq(int irq, int priority)
{
  uintptr_t regaddr;
  uint32_t regval;
  int shift;

  if (irq < 0 || irq >= NR_IRQS || priority < 0 || priority > 0xff)
    {
      return -EINVAL;
    }

  if (irq < NVIC_IRQ_FIRST)
    {
      if (irq < 4)
        {
          return -EINVAL;
        }

      regaddr = NVIC_SYSH_PRIORITY(irq);
      irq -= 4;
    }
  else
    {
      irq -= NVIC_IRQ_FIRST;
      regaddr = NVIC_IRQ_PRIORITY(irq);
    }

  shift = (irq & 3) << 3;
  regval = getreg32(regaddr);
  regval &= ~(UINT32_C(0xff) << shift);
  regval |= (uint32_t)priority << shift;
  putreg32(regval, regaddr);
  return OK;
}

void up_disable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t bit;

  if (bk7258_irqinfo(irq, &regaddr, &bit,
                      NVIC_IRQ0_31_CLEAR - NVIC_IRQ0_31_ENABLE) == OK)
    {
      if (irq >= NVIC_IRQ_FIRST)
        {
          putreg32(bit, regaddr);
        }
      else
        {
          modifyreg32(regaddr, bit, 0);
        }
    }
}

void up_enable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t bit;

  if (bk7258_irqinfo(irq, &regaddr, &bit, 0) == OK)
    {
      if (irq >= NVIC_IRQ_FIRST)
        {
          putreg32(bit, regaddr);
        }
      else
        {
          modifyreg32(regaddr, 0, bit);
        }
    }
}

void arm_ack_irq(int irq)
{
  (void)irq;
}

void up_irqinitialize(void)
{
  int irq;

  for (irq = 0; irq < NR_IRQS - NVIC_IRQ_FIRST; irq += 32)
    {
      putreg32(UINT32_MAX, NVIC_IRQ_CLEAR(irq));
      putreg32(UINT32_MAX, NVIC_IRQ_CLRPEND(irq));
    }

  putreg32((uintptr_t)_vectors, NVIC_VECTAB);
  irq_attach(NVIC_IRQ_SVCALL, arm_svcall, NULL);
  irq_attach(NVIC_IRQ_HARDFAULT, arm_hardfault, NULL);
  irq_attach(NVIC_IRQ_MEMFAULT, arm_memfault, NULL);
  irq_attach(NVIC_IRQ_BUSFAULT, arm_busfault, NULL);
  irq_attach(NVIC_IRQ_USAGEFAULT, arm_usagefault, NULL);
  irq_attach(NVIC_IRQ_SECUREFAULT, arm_securefault, NULL);

  modifyreg32(NVIC_SYSHCON, 0,
              NVIC_SYSHCON_MEMFAULTENA | NVIC_SYSHCON_BUSFAULTENA |
              NVIC_SYSHCON_USGFAULTENA | NVIC_SYSHCON_SECUREFAULTENA);
  up_prioritize_irq(NVIC_IRQ_PENDSV, NVIC_SYSH_PRIORITY_MIN);

  /* SVC drives NuttX thread-mode context switches. It must remain above the
   * BASEPRI level used to mask normal interrupts in critical sections. */

  up_prioritize_irq(NVIC_IRQ_SVCALL, NVIC_SYSH_SVCALL_PRIORITY);
  up_irq_enable();
}

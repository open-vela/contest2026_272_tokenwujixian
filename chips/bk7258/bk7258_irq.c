/****************************************************************************
 * chips/bk7258/bk7258_irq.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_internal.h"

extern const void *const _vectors[];

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

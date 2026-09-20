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
#include "sau.h"
#include "bk7258_internal.h"
#include "include/bk7258_irq.h"
#include "include/bk7258_memorymap.h"

extern const void *const _vectors[];

struct bk7258_vendor_irq_s
{
  void (*handler)(void);
  void *arg;
};

static struct bk7258_vendor_irq_s g_vendor_irq[64];

#define BK7258_FAULT_MAGIC   UINT32_C(0x424b4654) /* "BKFT" */
#define BK7258_FAULT_VERSION UINT32_C(2)

enum bk7258_fault_kind_e
{
  BK7258_FAULT_HARD = 1,
  BK7258_FAULT_MEM = 2,
  BK7258_FAULT_BUS = 3,
  BK7258_FAULT_USAGE = 4,
  BK7258_FAULT_SECURE = 5,
};

/* The generated CP linker script places .noinit before _sbss, so startup's
 * explicit BSS clear leaves this record intact over a CPU-local restart. */

struct bk7258_fault_record_s
{
  uint32_t magic;          /* Written last. */
  uint32_t version;
  uint32_t kind;
  uint32_t pc;
  uint32_t sp;
  uint32_t lr;
  uint32_t xpsr;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t bfar;
  uint32_t afsr;
  uint32_t aon_pmu_r0;
  uint32_t sfsr;
  uint32_t sfar;
};

static volatile struct bk7258_fault_record_s g_bk7258_fault_record
  __attribute__((section(".noinit"), used, aligned(8)));

static void bk7258_fault_capture(unsigned int kind, void *context,
                                 uint32_t sfsr, uint32_t sfar)
{
  uint32_t *regs = context;
  volatile struct bk7258_fault_record_s *record =
    &g_bk7258_fault_record;

  /* No logging, locks, allocation, or scheduler calls are safe in a fault
   * context.  Invalidate first, commit magic last, and preserve NuttX's
   * normal handler/panic behavior in the wrapper below. */

  record->magic = 0;
  record->version = BK7258_FAULT_VERSION;
  record->kind = kind;
  record->pc = regs != NULL ? regs[REG_PC] : 0;
  record->sp = regs != NULL ? regs[REG_SP] : 0;
  record->lr = regs != NULL ? regs[REG_LR] : 0;
  record->xpsr = regs != NULL ? regs[REG_XPSR] : 0;
  record->cfsr = getreg32(NVIC_CFAULTS);
  record->hfsr = getreg32(NVIC_HFAULTS);
  record->bfar = getreg32(NVIC_BFAULT_ADDR);
  record->afsr = getreg32(NVIC_AFAULTS);
  record->aon_pmu_r0 = getreg32(BK7258_AON_PMU_BASE);
  record->sfsr = sfsr;
  record->sfar = sfar;
  __asm__ volatile ("dsb" : : : "memory");
  record->magic = BK7258_FAULT_MAGIC;
  __asm__ volatile ("dsb\n\tisb" : : : "memory");
}

int bk7258_hardfault(int irq, void *context, void *arg)
{
  bk7258_fault_capture(BK7258_FAULT_HARD, context, 0, 0);
  return arm_hardfault(irq, context, arg);
}

int bk7258_memfault(int irq, void *context, void *arg)
{
  bk7258_fault_capture(BK7258_FAULT_MEM, context, 0, 0);
  return arm_memfault(irq, context, arg);
}

int bk7258_busfault(int irq, void *context, void *arg)
{
  bk7258_fault_capture(BK7258_FAULT_BUS, context, 0, 0);
  return arm_busfault(irq, context, arg);
}

int bk7258_usagefault(int irq, void *context, void *arg)
{
  bk7258_fault_capture(BK7258_FAULT_USAGE, context, 0, 0);
  return arm_usagefault(irq, context, arg);
}

int bk7258_securefault(int irq, void *context, void *arg)
{
  uint32_t sfsr = getreg32(SAU_SFSR);
  uint32_t sfar = (sfsr & SAU_SFSR_SFARVALID) != 0 ?
                  getreg32(SAU_SFAR) : 0;

  bk7258_fault_capture(BK7258_FAULT_SECURE, context, sfsr, sfar);
  return arm_securefault(irq, context, arg);
}

static void bk7258_fault_puthex(uint32_t value)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for (shift = 28; shift >= 0; shift -= 4)
    {
      bk7258_lowputc(hex[(value >> shift) & 0xf]);
    }
}

static void bk7258_fault_puts(const char *str)
{
  while (*str != '\0')
    {
      bk7258_lowputc(*str++);
    }
}

void bk7258_fault_report_early(void)
{
  volatile struct bk7258_fault_record_s *record =
    &g_bk7258_fault_record;

  if (record->magic != BK7258_FAULT_MAGIC ||
      record->version != BK7258_FAULT_VERSION)
    {
      return;
    }

  bk7258_fault_puts("!FAULT k=");
  bk7258_fault_puthex(record->kind);
  bk7258_fault_puts(" pc=");
  bk7258_fault_puthex(record->pc);
  bk7258_fault_puts(" sp=");
  bk7258_fault_puthex(record->sp);
  bk7258_fault_puts(" lr=");
  bk7258_fault_puthex(record->lr);
  bk7258_fault_puts(" xpsr=");
  bk7258_fault_puthex(record->xpsr);
  bk7258_fault_puts(" cfsr=");
  bk7258_fault_puthex(record->cfsr);
  bk7258_fault_puts(" hfsr=");
  bk7258_fault_puthex(record->hfsr);
  bk7258_fault_puts(" bfar=");
  bk7258_fault_puthex(record->bfar);
  bk7258_fault_puts(" afsr=");
  bk7258_fault_puthex(record->afsr);
  bk7258_fault_puts(" r0=");
  bk7258_fault_puthex(record->aon_pmu_r0);
  bk7258_fault_puts(" sfsr=");
  bk7258_fault_puthex(record->sfsr);
  bk7258_fault_puts(" sfar=");
  bk7258_fault_puthex(record->sfar);
  bk7258_fault_puts("\r\n");

  /* Consume after output so a later unrelated soft reset does not report an
   * old fault.  A new fault will invalidate/commit its own record. */
  record->magic = 0;
  __asm__ volatile ("dsb" : : : "memory");
}

static int bk7258_vendor_irq_handler(int irq, void *context, void *arg)
{
  struct bk7258_vendor_irq_s *entry = arg;

  (void)irq;
  (void)context;
  if (entry != NULL && entry->handler != NULL)
    {
      entry->handler();
    }
  return OK;
}

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

  /* Errors only.  This is not a hot path by design, but the temperature/voltage
   * sampler attaches and detaches the SARADC interrupt on every cycle -- once a
   * second for the first 30 samples -- so an unconditional record here buried
   * the wpa_supplicant trace that carries actual association failures. */

  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258] ICU attach source=%u irq=%d ret=%d\n",
             source, irq, ret);
    }

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

  /* Dropped for the same reason as the attach record above: both early returns
   * already report their own failures, so nothing diagnostic is lost. */

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

  if (ret < 0)
    {
      syslog(LOG_ERR,
             "[BK7258] ICU disable source=%u irq=%d ret=%d int_en=0x%08lx\n",
             source, irq, ret,
             (unsigned long)getreg32(BK7258_SYS_CPU0_INT_EN));
    }

  return ret;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

int bk7258_icu_vendor_register(unsigned int source, void (*handler)(void),
                               void *arg)
{
  int ret;
  irqstate_t flags;

  if (source >= 64 || handler == NULL)
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  if (g_vendor_irq[source].handler != NULL)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  g_vendor_irq[source].handler = handler;
  g_vendor_irq[source].arg = arg;
  ret = bk7258_icu_attach(source, bk7258_vendor_irq_handler,
                           &g_vendor_irq[source]);
  if (ret < 0)
    {
      g_vendor_irq[source].handler = NULL;
      g_vendor_irq[source].arg = NULL;
    }
  leave_critical_section(flags);
  return ret;
}

int bk7258_icu_vendor_unregister(unsigned int source)
{
  irqstate_t flags;
  int ret;

  if (source >= 64)
    {
      return -EINVAL;
    }

  ret = bk7258_icu_disable(source);
  irq_detach(BK7258_IRQ_FIRST + source);
  flags = enter_critical_section();
  g_vendor_irq[source].handler = NULL;
  g_vendor_irq[source].arg = NULL;
  leave_critical_section(flags);
  return ret;
}

int bk7258_icu_set_priority(unsigned int source, int priority)
{
  if (source >= 64)
    {
      return -EINVAL;
    }

  return up_prioritize_irq(BK7258_IRQ_FIRST + source, priority);
}

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

  /* Polling-only BK-only boot ladder: this runs before the scheduler and
   * intentionally does not depend on syslog or a registered console. */
  BK7258_BOOT_MARK('A');

  for (irq = 0; irq < NR_IRQS - NVIC_IRQ_FIRST; irq += 32)
    {
      putreg32(UINT32_MAX, NVIC_IRQ_CLEAR(irq));
      putreg32(UINT32_MAX, NVIC_IRQ_CLRPEND(irq));
    }

  BK7258_BOOT_MARK('B');

  putreg32((uintptr_t)_vectors, NVIC_VECTAB);
  irq_attach(NVIC_IRQ_SVCALL, arm_svcall, NULL);
  irq_attach(NVIC_IRQ_HARDFAULT, bk7258_hardfault, NULL);
  irq_attach(NVIC_IRQ_MEMFAULT, bk7258_memfault, NULL);
  irq_attach(NVIC_IRQ_BUSFAULT, bk7258_busfault, NULL);
  irq_attach(NVIC_IRQ_USAGEFAULT, bk7258_usagefault, NULL);
  irq_attach(NVIC_IRQ_SECUREFAULT, bk7258_securefault, NULL);

  modifyreg32(NVIC_SYSHCON, 0,
              NVIC_SYSHCON_MEMFAULTENA | NVIC_SYSHCON_BUSFAULTENA |
              NVIC_SYSHCON_USGFAULTENA | NVIC_SYSHCON_SECUREFAULTENA);
  up_prioritize_irq(NVIC_IRQ_PENDSV, NVIC_SYSH_PRIORITY_MIN);

  BK7258_BOOT_MARK('C');

  /* SVC drives NuttX thread-mode context switches. It must remain above the
   * BASEPRI level used to mask normal interrupts in critical sections. */

  up_prioritize_irq(NVIC_IRQ_SVCALL, NVIC_SYSH_SVCALL_PRIORITY);
  up_irq_enable();

  BK7258_BOOT_MARK('D');
}

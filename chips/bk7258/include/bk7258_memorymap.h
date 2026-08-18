/****************************************************************************
 * chips/bk7258/include/bk7258_memorymap.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_MEMORYMAP_H
#define __VENDOR_BEKEN_CHIP_BK7258_MEMORYMAP_H

#include <stdint.h>

/* Locked L0 secure/SPE address domain. */

#define BK7258_FLASH_BASE        UINT32_C(0x02000000)
#define BK7258_FLASH_VECTOR_BASE UINT32_C(0x02010000)

/* SoC-level SRAM invariant: the whole on-chip window, shared by CP, AP and the
 * cross-domain regions below. How that window is split between CP and AP is a
 * board decision, so no CP or AP carveout belongs here. A component's own RAM
 * window reaches C code as CONFIG_RAM_START / CONFIG_RAM_SIZE / CONFIG_RAM_END,
 * which is also what the upstream arch heap allocators use.
 */

#define BK7258_SRAM_CAPACITY_BASE UINT32_C(0x28000000)
#define BK7258_SRAM_CAPACITY_SIZE UINT32_C(0x000a0000)
#define BK7258_SRAM_CAPACITY_END  (BK7258_SRAM_CAPACITY_BASE + \
                                   BK7258_SRAM_CAPACITY_SIZE)

/* Cross-domain regions. They belong to the CP/AP contract rather than to either
 * image, so no component may place .data, .bss, a stack or a heap over them.
 *
 * PWR_MNG carries state both domains read and write: power/clock votes, wakeup
 * counters, reset reason, exception arbitration and the cross-domain Flash lock.
 * SWAP is the shared buffer area; AP bring-up writes a small versioned boot
 * record there before any RPMsg carveout exists.
 *
 * The addresses below are the all-OpenVela firmware tuple. It keeps the locked
 * CP/PWR_MNG/SWAP addresses but replaces the vendor AP and HARDWARE_ACC owners:
 * OpenVela AP receives 0x28010000..0x28064000 and the board Kconfig permanently
 * reserves 0x28064000..0x2806ec00 for RPMsg.
 *
 * The RPMsg carveout is deliberately not here. It is a split of the SoC SRAM
 * between components, which is a board decision, and it has to reach both the
 * linker script and the rptun driver -- so it lives in the board Kconfig as
 * CONFIG_BK7258_RPMSG_SHM_ADDR/SIZE, the only source both can read.
 */

#define BK7258_AP_SPINLOCK_BASE  UINT32_C(0x28000000)
#define BK7258_AP_SPINLOCK_SIZE  UINT32_C(0x00010000)

#define BK7258_PWR_MNG_BASE      UINT32_C(0x2809f700)
#define BK7258_PWR_MNG_SIZE      UINT32_C(0x00000100)

#define BK7258_SWAP_BASE         UINT32_C(0x2809f800)
#define BK7258_SWAP_SIZE         UINT32_C(0x00000800)

#define BK7258_AP_XIP_VECTOR_BASE UINT32_C(0x02160000)
#define BK7258_AP_XIP_SIZE        UINT32_C(0x002a0000)
#define BK7258_AP_RAM_BASE        UINT32_C(0x28010000)
#define BK7258_AP_RAM_SIZE        UINT32_C(0x00054000)
#define BK7258_AP_RAM_END         (BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE)

#define BK7258_SYS_BASE          UINT32_C(0x44010000)
#define BK7258_AON_GPIO_BASE     UINT32_C(0x44000400)
#define BK7258_UART0_BASE        UINT32_C(0x44820000)

#define BK7258_SYS_CLKDIV1       (BK7258_SYS_BASE + UINT32_C(0x20))
#define BK7258_SYS_DEV_CLK_EN    (BK7258_SYS_BASE + UINT32_C(0x30))
#define BK7258_SYS_POWER_WAKEUP  (BK7258_SYS_BASE + UINT32_C(0x40))
#define BK7258_SYS_GPIO_FUNC0    (BK7258_SYS_BASE + UINT32_C(0x0c0))
#define BK7258_SYS_CPU0_INT_EN   (BK7258_SYS_BASE + UINT32_C(0x080))
#define BK7258_SYS_CPU0_INT_EN_HI (BK7258_SYS_BASE + UINT32_C(0x084))
#define BK7258_SYS_CPU1_INT_EN_HI (BK7258_SYS_BASE + UINT32_C(0x08c))
#define BK7258_SYS_CPU1_CTRL     (BK7258_SYS_BASE + UINT32_C(0x014))
#define BK7258_SYS_CPU_STATUS    (BK7258_SYS_BASE + UINT32_C(0x00c))
#define BK7258_MBOX0_BASE        UINT32_C(0x41000000)

#define BK7258_UART_GLOBAL_CTRL  (BK7258_UART0_BASE + UINT32_C(0x08))
#define BK7258_UART_CONFIG       (BK7258_UART0_BASE + UINT32_C(0x10))
#define BK7258_UART_FIFO_STATUS  (BK7258_UART0_BASE + UINT32_C(0x18))
#define BK7258_UART_FIFO_PORT    (BK7258_UART0_BASE + UINT32_C(0x1c))
#define BK7258_UART_INT_ENABLE   (BK7258_UART0_BASE + UINT32_C(0x20))
#define BK7258_UART_INT_STATUS   (BK7258_UART0_BASE + UINT32_C(0x24))

#define BK7258_UART_FIFO_WR_READY (UINT32_C(1) << 20)
#define BK7258_UART_FIFO_RD_READY (UINT32_C(1) << 21)

#define BK7258_UART_CONFIG_TX_EN  (UINT32_C(1) << 0)
#define BK7258_UART_CONFIG_RX_EN  (UINT32_C(1) << 1)
#define BK7258_UART_CONFIG_8BITS  (UINT32_C(3) << 3)
#define BK7258_UART_CONFIG_DIV(n) (((uint32_t)(n) & UINT32_C(0xffff)) << 8)

#define BK7258_UART_INT_TX_READY  (UINT32_C(1) << 0)
#define BK7258_UART_INT_RX_READY  (UINT32_C(1) << 1)
#define BK7258_UART_INT_RX_FINISH (UINT32_C(1) << 6)

#define BK7258_SYS_UART0_CLK_EN   (UINT32_C(1) << 2)
#define BK7258_SYS_UART0_XTAL     (UINT32_C(1) << 10)
#define BK7258_SYS_UART0_INT_EN   (UINT32_C(1) << 4)
#define BK7258_SYS_CPU0_SYSTICK_32K (UINT32_C(1) << 29)
#define BK7258_SYS_CPU1_SYSTICK_32K (UINT32_C(1) << 30)
#define BK7258_SYS_SHARE_MEM_CLKGATING_DISABLE (UINT32_C(1) << 28)
#define BK7258_SYS_MAILBOX_INT_EN (UINT32_C(1) << 31)

#define BK7258_SYS_CPU1_RESET_RELEASE (UINT32_C(1) << 0)
#define BK7258_SYS_CPU1_POWER_DOWN    (UINT32_C(1) << 1)
#define BK7258_SYS_CPU1_HALT          (UINT32_C(1) << 3)
#define BK7258_SYS_CPU1_RXEVT_SEL     (UINT32_C(1) << 5)
#define BK7258_SYS_CPU1_OFFSET_MASK   UINT32_C(0xffffff00)
#define BK7258_SYS_CPU1_HALTED_STATE  (UINT32_C(1) << 1)
#define BK7258_SYS_CPU1_RESET_STATE   (UINT32_C(1) << 5)
#define BK7258_SYS_CPU1_PWR_DW_STATE  (UINT32_C(1) << 9)

#define BK7258_GPIO11             11
#define BK7258_GPIO_CFG(index)    (BK7258_AON_GPIO_BASE + ((uint32_t)(index) << 2))
#define BK7258_GPIO_FUNC_SHIFT(pin) (((uint32_t)(pin) & 7) << 2)

#endif /* __VENDOR_BEKEN_CHIP_BK7258_MEMORYMAP_H */

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

/* External PSRAM is a CPU-visible data window. The controller can map up to
 * 64 MiB, but the fitted-device size is discovered before any portion is
 * offered to the allocator. */

#define BK7258_PSRAM_BASE          UINT32_C(0x60000000)
#define BK7258_PSRAM_WINDOW_SIZE   UINT32_C(0x04000000)

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
#define BK7258_TIMER0_BASE       UINT32_C(0x44810000)

#define BK7258_SYS_CLKDIV1       (BK7258_SYS_BASE + UINT32_C(0x20))
#define BK7258_SYS_CLKDIV2       (BK7258_SYS_BASE + UINT32_C(0x24))
#define BK7258_SYS_DEV_CLK_EN    (BK7258_SYS_BASE + UINT32_C(0x30))
#define BK7258_SYS_DEV_MEM_CTRL1 (BK7258_SYS_BASE + UINT32_C(0x38))
#define BK7258_SYS_DEV_MEM_CTRL2 (BK7258_SYS_BASE + UINT32_C(0x3c))
#define BK7258_SYS_POWER_WAKEUP  (BK7258_SYS_BASE + UINT32_C(0x40))
#define BK7258_SYS_ANA_REG13     (BK7258_SYS_BASE + UINT32_C(0x134))
#define BK7258_SYS_GPIO_FUNC0    (BK7258_SYS_BASE + UINT32_C(0x0c0))
#define BK7258_SYS_CPU0_INT_EN   (BK7258_SYS_BASE + UINT32_C(0x080))
#define BK7258_SYS_CPU0_INT_EN_HI (BK7258_SYS_BASE + UINT32_C(0x084))
#define BK7258_SYS_CPU1_INT_EN   (BK7258_SYS_BASE + UINT32_C(0x088))
#define BK7258_SYS_CPU1_INT_EN_HI (BK7258_SYS_BASE + UINT32_C(0x08c))
#define BK7258_SYS_CPU2_INT_EN   (BK7258_SYS_BASE + UINT32_C(0x090))
#define BK7258_SYS_CPU2_INT_EN_HI (BK7258_SYS_BASE + UINT32_C(0x094))
#define BK7258_SYS_CPU1_CTRL     (BK7258_SYS_BASE + UINT32_C(0x014))
#define BK7258_SYS_CPU2_CTRL     (BK7258_SYS_BASE + UINT32_C(0x018))
#define BK7258_SYS_CPU_STATUS    (BK7258_SYS_BASE + UINT32_C(0x00c))
#define BK7258_MBOX0_BASE        UINT32_C(0x41000000)
#define BK7258_GDMA0_BASE        UINT32_C(0x45020000)
#define BK7258_PSRAM_CTRL_BASE   UINT32_C(0x46080000)

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
#define BK7258_SYS_GDMA0_INT_EN   (UINT32_C(1) << 11)
#define BK7258_SYS_TIMER0_CLK_EN  (UINT32_C(1) << 4)
#define BK7258_SYS_PSRAM_CLK_EN   (UINT32_C(1) << 19)
#define BK7258_SYS_TIMER0_XTAL   (UINT32_C(1) << 20)
#define BK7258_SYS_MAC_CKEN       (UINT32_C(1) << 26)
#define BK7258_SYS_PHY_CKEN       (UINT32_C(1) << 27)
#define BK7258_SYS_WIFI_MAC_POWERDOWN (UINT32_C(1) << 9)
#define BK7258_SYS_WIFI_PHY_POWERDOWN (UINT32_C(1) << 10)

#define BK7258_TIMER0_GLOBAL_CTRL  (BK7258_TIMER0_BASE + UINT32_C(0x08))
#define BK7258_TIMER0_COUNT0       (BK7258_TIMER0_BASE + UINT32_C(0x10))
#define BK7258_TIMER0_CTRL         (BK7258_TIMER0_BASE + UINT32_C(0x1c))
#define BK7258_TIMER0_ENABLE       (UINT32_C(1) << 0)
#define BK7258_TIMER0_INT_ENABLE   (UINT32_C(1) << 7)

/* BK7258 PSRAM controller registers and system control fields. These values
 * are from Armino's BK7258 CP register definitions and are used only by the
 * hardware initialization sequence in bk7258_psram.c. */

#define BK7258_PSRAM_REG2          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x08))
#define BK7258_PSRAM_REG4          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x10))
#define BK7258_PSRAM_REG5          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x14))
#define BK7258_PSRAM_REG8          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x20))
#define BK7258_PSRAM_REG9          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x24))
#define BK7258_PSRAM_REGA          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x28))
#define BK7258_PSRAM_REGB          (BK7258_PSRAM_CTRL_BASE + UINT32_C(0x2c))

#define BK7258_PSRAM_REG2_SF_RESET (UINT32_C(1) << 0)
#define BK7258_PSRAM_REG2_BYPASS   (UINT32_C(1) << 1)
#define BK7258_PSRAM_REG8_WRITE    UINT32_C(0x1)
#define BK7258_PSRAM_REG8_READ     UINT32_C(0x2)
#define BK7258_PSRAM_REG8_RESET    UINT32_C(0x4)

#define BK7258_SYS_CLKDIV2_PSRAM_DIV (UINT32_C(1) << 4)
#define BK7258_SYS_CLKDIV2_PSRAM_SEL (UINT32_C(1) << 5)
#define BK7258_SYS_ANA_REG13_PSLDO_SWB (UINT32_C(1) << 28)
#define BK7258_SYS_ANA_REG13_VPSRAMSEL_MASK (UINT32_C(3) << 29)
#define BK7258_SYS_ANA_REG13_ENPSRAM (UINT32_C(1) << 31)

/* ICU group 0/1 enable masks.  ICU 37 shares the group-1 bit with HSU in
 * Armino's register contract and is intentionally represented by one mask. */
#define BK7258_SYS_IRQ_GROUP0(source) (UINT32_C(1) << (source))
#define BK7258_SYS_IRQ_GROUP1(source) (UINT32_C(1) << ((source) - 32))
#define BK7258_SYS_CPU0_SYSTICK_32K (UINT32_C(1) << 29)
#define BK7258_SYS_CPU1_SYSTICK_32K (UINT32_C(1) << 30)
#define BK7258_SYS_SHARE_MEM_CLKGATING_DISABLE (UINT32_C(1) << 28)
#define BK7258_SYS_AUD_MEM_LOW_POWER (UINT32_C(1) << 11)
#define BK7258_SYS_AUDP_POWER_DOWN (UINT32_C(1) << 6)
#define BK7258_SYS_GPIO_INT_EN   (UINT32_C(1) << 23)
#define BK7258_SYS_MAILBOX_INT_EN (UINT32_C(1) << 31)

#define BK7258_SYS_CPU1_RESET_RELEASE (UINT32_C(1) << 0)
#define BK7258_SYS_CPU1_POWER_DOWN    (UINT32_C(1) << 1)
#define BK7258_SYS_CPU1_HALT          (UINT32_C(1) << 3)
#define BK7258_SYS_CPU1_RXEVT_SEL     (UINT32_C(1) << 5)
#define BK7258_SYS_CPU1_OFFSET_MASK   UINT32_C(0xffffff00)
#define BK7258_SYS_CPU_BOOT_OFFSET(address) \
  (((uint32_t)(address) >> 8) << 8)
#define BK7258_SYS_CPU1_HALTED_STATE  (UINT32_C(1) << 1)
#define BK7258_SYS_CPU1_RESET_STATE   (UINT32_C(1) << 5)
#define BK7258_SYS_CPU1_PWR_DW_STATE  (UINT32_C(1) << 9)

/* Physical CPU2 (AP SMP secondary core) uses the same SYS_CPUx_CTRL bit
 * layout as CPU1.  SYS_CPU_STATUS groups one state per four bits with one
 * field per physical core: halted/reset/power-down occupy bits 0-2, 4-6 and
 * 8-10 respectively, so CPU2 shifts each CPU1 bit by one.
 */

#define BK7258_SYS_CPU2_RESET_RELEASE (UINT32_C(1) << 0)
#define BK7258_SYS_CPU2_POWER_DOWN    (UINT32_C(1) << 1)
#define BK7258_SYS_CPU2_HALT          (UINT32_C(1) << 3)
#define BK7258_SYS_CPU2_RXEVT_SEL     (UINT32_C(1) << 5)
#define BK7258_SYS_CPU2_OFFSET_MASK   UINT32_C(0xffffff00)
#define BK7258_SYS_CPU2_HALTED_STATE  (UINT32_C(1) << 2)
#define BK7258_SYS_CPU2_RESET_STATE   (UINT32_C(1) << 6)
#define BK7258_SYS_CPU2_PWR_DW_STATE  (UINT32_C(1) << 10)

#define BK7258_GPIO11             11

#define BK7258_GPIO_CFG(index)    (BK7258_AON_GPIO_BASE + ((uint32_t)(index) << 2))

/* AON GPIO per-pin configuration register bit fields.  These match the
 * Armino SDK gpio_struct.h / gpio_ll.h layout used by the board bring-up:
 *
 *   bit 0          sampled input value
 *   bit 1          output latch value
 *   bits [3:2]     IO mode: 0=output, 2=I/O disable, 3=input
 *   bits [5:4]     pull: 0=none, 2=pull-down, 3=pull-up
 *   bit 6          second function enable (1=peripheral drives the pad)
 *   bit 7          input monitor control
 *   bits [9:8]     drive capacity
 *   bits [11:10]   interrupt type: low/high/rising/falling = 0/1/2/3
 *   bit 12         interrupt enable
 *   bit 13         interrupt clear
 */

#define BK7258_GPIO_CFG_INPUT       (UINT32_C(1) << 0)
#define BK7258_GPIO_CFG_OUTPUT      (UINT32_C(1) << 1)
#define BK7258_GPIO_CFG_MODE_MASK   (UINT32_C(3) << 2)
#define BK7258_GPIO_CFG_MODE_OUTPUT (UINT32_C(0) << 2)
#define BK7258_GPIO_CFG_MODE_INPUT  (UINT32_C(3) << 2)
#define BK7258_GPIO_CFG_MODE_DIS    (UINT32_C(2) << 2)
#define BK7258_GPIO_CFG_PULL_MASK   (UINT32_C(3) << 4)
#define BK7258_GPIO_CFG_PULL_DOWN   (UINT32_C(2) << 4)
#define BK7258_GPIO_CFG_PULL_UP     (UINT32_C(3) << 4)
#define BK7258_GPIO_CFG_SECOND_FUNC (UINT32_C(1) << 6)
#define BK7258_GPIO_INT_TYPE_MASK   (UINT32_C(3) << 10)
#define BK7258_GPIO_INT_LOW_LEVEL   (UINT32_C(0) << 10)
#define BK7258_GPIO_INT_HIGH_LEVEL  (UINT32_C(1) << 10)
#define BK7258_GPIO_INT_RISING_EDGE (UINT32_C(2) << 10)
#define BK7258_GPIO_INT_FALLING_EDGE (UINT32_C(3) << 10)
#define BK7258_GPIO_INT_ENABLE      (UINT32_C(1) << 12)
#define BK7258_GPIO_INT_CLEAR       (UINT32_C(1) << 13)

/* SYS_GPIO_FUNC0 registers hold a 4-bit peripheral mux field per pin,
 * eight pins per register, covering GPIO0 through GPIO55.  A cleared field
 * selects the plain GPIO function of the pin.
 */

#define BK7258_SYS_GPIO_FUNC(pin) \
  (BK7258_SYS_GPIO_FUNC0 + (((uint32_t)(pin) >> 3) << 2))
#define BK7258_GPIO_FUNC_MASK(pin) \
  (UINT32_C(0xf) << (((uint32_t)(pin) & 7) << 2))

#endif /* __VENDOR_BEKEN_CHIP_BK7258_MEMORYMAP_H */

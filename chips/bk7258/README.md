# BK7258 芯片层占位目录

此目录承载所有与具体开发板无关的 BK7258 芯片层实现，并由仓库 manifest 映射到 `vendor/beken/chips/bk7258`。

未来内容包括启动入口、内存与堆初始化、时钟、中断、系统定时器、早期控制台和 UART/GPIO/I2C/SPI 控制器支持。实际寄存器、时钟、IRQ、内存布局和编译配置必须以 BK7258 TRM、可用 SDK/BSP 与上游适配规范为准。

CPU0 的启动、内存、链接与早期 UART 设计边界见 [BK7258 CPU0 Bring-up Contract](BK7258_CPU0_BRINGUP_CONTRACT.md)。该合同记录已确认的 Armino CP 产物和 DevKit 原理图证据，并明确尚不能写入 BSP 的未知项。

当前仅建立目录和构建接入占位；这里没有可选 Kconfig 目标、编译源文件、链接脚本或可启动实现。

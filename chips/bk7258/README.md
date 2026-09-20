# BK7258 芯片层与 Wi-Fi 适配目录

此目录承载所有与具体开发板无关的 BK7258 芯片层实现，并由仓库 manifest 映射到 `vendor/beken/chips/bk7258`。

未来内容包括启动入口、内存与堆初始化、时钟、中断、系统定时器、早期控制台和 UART/GPIO/I2C/SPI 控制器支持。实际寄存器、时钟、IRQ、内存布局和编译配置必须以 BK7258 TRM、可用 SDK/BSP 与上游适配规范为准。

CPU0 的启动、内存、链接与早期 UART 设计边界见 [BK7258 CPU0 Bring-up Contract](BK7258_CPU0_BRINGUP_CONTRACT.md)。该合同记录已确认的 Armino CP 产物和 DevKit 原理图证据，并明确尚不能写入 BSP 的未知项。

芯片层启动、时钟、中断、DMA、UART 等实现已落地；Wi-Fi 适配已进入
STA-only runtime bring-up。当前已具备 NuttX OSAL、vendor packet/pbuf
边界拷贝、netdev lower-half、IRQ、SARADC 生命周期、vendor archive ABI
挂载和独立 runtime probe。默认 CP 仍关闭 vendor runtime；runtime probe
仅用于源码/链接闭包验证，尚无烧录或实板联网证据。

当前功能阶段是“扫描 → WPA/EAPOL 建立 → 连接 → 断开/状态 → DHCP”。
SoftAP、P2P、WPS、monitor、STA+AP 并发和 BT coexistence 暂不实现。

详细进度见 [BK7258 Wi-Fi capability status](wifi/CAPABILITY_TABLE_STATUS.md) 和
[Wi-Fi feature note](BK7258_WIFI_FEATURE.md)。

STA 适配方案与架构原则（原 `OPENVELA_NUTTX_WIFI_PORTING_PLAN.md`）已迁出本仓库，现位于
`/home/czp/openvela_contest/my_docs/bk7258-wifi/plan/`；同处 `my_docs/bk7258-wifi/` 下还有
排查过程记录与方法论复盘，总索引见该目录 `README.md`。

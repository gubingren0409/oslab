# VisionFive 2 移植准备

## 目标与边界

最终目标是在 StarFive VisionFive 2 上启动本实验内核。当前阶段只做不会影响 Lab1
QEMU 验收的准备：记录硬件约束、保留启动参数、规划平台抽象并补齐构建和镜像检查工具。
在拿到开发板及助教最终固件规范前，不刷写 SPI Flash，也不把未经硬件验证的代码并入稳定分支。

## 已确认的硬件与启动约束

- VisionFive 2 使用 StarFive JH7110，包含 4 个可运行内核的 U74 应用 hart；应用 hart ID
  通常是 1～4，而不是 QEMU 常见的从 0 开始。参见
  [U-Boot JH7110 文档](https://github.com/u-boot/u-boot/blob/master/doc/board/starfive/jh7110_common.rst)。
- DRAM 从 `0x40000000` 开始。固件会占用并保留部分低地址内存，内核不能把整段 DRAM
  当成可分配页。参见
  [Linux JH7110 公共设备树](https://github.com/torvalds/linux/blob/master/arch/riscv/boot/dts/starfive/jh7110-common.dtsi)。
- 标准启动链是 BootROM -> SPL -> OpenSBI -> U-Boot -> 内核；S-mode 内核应保留固件传入的
  `a0`（hart ID）与 `a1`（DTB 地址）。参见
  [Linux RISC-V 启动约定](https://docs.kernel.org/arch/riscv/boot.html) 和
  [OpenSBI 固件文档](https://github.com/riscv-software-src/opensbi/blob/master/docs/firmware/fw.md)。
- UART0 的基址仍为 `0x10000000`，但 JH7110 使用 DesignWare APB UART，寄存器间隔和访问宽度
  与 Lab1 的 QEMU 16550 配置不同，不能只复用现有字节访问宏。参见
  [Linux JH7110 设备树](https://github.com/torvalds/linux/blob/master/arch/riscv/boot/dts/starfive/jh7110.dtsi)。
- VisionFive 2 没有 QEMU 的 VirtIO 块设备。后期需要 SD/eMMC 驱动，或在早期启动阶段先使用
  initramfs/内存盘。

## 与当前 QEMU 环境的关键差异

| 项目 | QEMU `virt` / Lab1 | VisionFive 2 |
| --- | --- | --- |
| 启动方式 | QEMU 直接装载 ELF，`-bios none` | SPL + OpenSBI + U-Boot 装载内核 |
| 当前入口/DRAM 基址 | `0x80000000` | DRAM 从 `0x40000000` 开始，装载地址由固件方案决定 |
| 应用 hart | 通常 0、1 | 通常 1～4，需要逻辑 CPU 映射 |
| UART | 16550，字节寄存器，IRQ 10 | DW APB UART，32 位访问/4 字节间隔，IRQ 32 |
| 定时器 | QEMU CLINT | 建议通过 SBI TIME，板上 timebase 为 4 MHz |
| 多核启动 | QEMU 同时启动 | 建议通过 SBI HSM 启动从 hart |
| 根存储 | VirtIO MMIO | SD/eMMC，驱动完全不同 |
| 硬件描述 | 当前为编译期常量 | 应优先读取 DTB |

## 代码设计约束

Lab1 期间保持现有 `make run` 和 QEMU 路径不变。开始实际移植时采用以下边界：

```text
arch/riscv/              RISC-V 通用寄存器与入口约定
platform/qemu_virt/      QEMU 地址、UART、PLIC、定时器后端
platform/visionfive2/    JH7110 UART、IRQ、hart 映射和镜像配置
boot/                    保存 a0/a1，解析 DTB，生成 BootInfo
```

需要从公共代码中移出的硬编码包括：RAM 范围、UART 寄存器宽度和偏移、IRQ 编号、
PLIC context、timer frequency、hart-to-CPU 映射以及块设备类型。公共内核只依赖
`console`、`timer`、`ipi/hsm`、`irqchip`、`blockdev` 和 `BootInfo` 接口。

`BootInfo` 至少应保存：

- 启动 hart 与可用 CPU 列表；
- 可用内存区间及固件保留区；
- DTB 地址或持久化后的必要字段；
- 控制台、PLIC、timer 和块设备描述。

## 分阶段验证

1. **Lab1～Lab2：** QEMU 路径保持可回归；入口处保留启动参数，避免继续扩散平台常量。
2. **无开发板阶段：** 为 `PLATFORM=qemu_virt` / `PLATFORM=visionfive2` 设计双构建目标，
   检查 ELF、装载地址和 DTB，不声称硬件可运行。
3. **上板最小闭环：** 从 U-Boot 临时加载内核，仅完成串口输出并停机；不改写板载固件。
4. **中断与 SMP：** 接入 SBI TIME/HSM、PLIC 和真实 hart 映射。
5. **存储与用户态：** 接入 SD/eMMC，或先用 initramfs 过渡，再验证完整文件系统。

## 当前工具准备

VMware 中的 Ubuntu 22.04.5 已补齐并验证以下工具：

| 工具 | 当前版本/状态 | 用途 |
| --- | --- | --- |
| Device Tree Compiler | 1.6.1 | DTB/DTS 编译与反编译 |
| U-Boot Tools (`mkimage`) | 2022.01 | U-Boot 可加载镜像的生成与检查 |
| `picocom` | 3.1 | 连接开发板 3.3 V 串口 |
| Git LFS | 3.0.2 | 获取可能使用 LFS 的板级源码或镜像 |
| bison/flex/bc/cpio 等 | 已就绪 | U-Boot、OpenSBI 和板级构建依赖 |

本次没有克隆约 9 GB 的完整 StarFive SDK，也没有刷写任何固件。虚拟机根盘仍有约
53 GB 可用；待助教给出固件版本或实体板到位后，再决定是否拉取 SDK，避免提前绑定过时版本。

## 硬件准备建议

不必为当前 Lab1 立即购买开发板。若希望提前购买，优先选择助教后续固定的 VisionFive 2，
并同时准备 3.3 V USB-TTL 串口、至少 32 GB microSD、读卡器和稳定的 Type-C 电源。
首次测试优先从 U-Boot/SD/TFTP 临时加载，保留原厂恢复链路。官方准备清单见
[VisionFive 2 Required Hardware](https://doc-en.rvspace.org/VisionFive2/Quick_Start_Guide/VisionFive2_QSG/required_hardware.html)。

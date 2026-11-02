# VisionFive 2 平台适配与验收

## 当前状态

课程lab5@4a0f4b72已正式要求VisionFive 2支持，Lab6在此基础上扩展多进程。
Lab5/Lab6已在既定VMware环境完成双平台构建；Lab5 QEMU双核和Lab6单/双核共10项
回归均通过。VF2 v1.3B、4GB已完成官方Ubuntu启动和Lab5四核实板基础验收。
Lab5实际启动hart为1，其他启动hart及Lab6实板测试尚待补齐，详见各实验报告。

## 平台参数

| 项目 | QEMU virt | VisionFive 2 / JH7110 |
| --- | --- | --- |
| DRAM起点 | 0x80000000 | 0x40000000 |
| 内核入口 | 0x80200000 | 0x40200000 |
| 管理窗口终点 | 0x88200000 | 0x48200000 |
| 有效hart | 默认0、1；Lab6支持单核0 | 1、2、3、4；数组保留5槽，直接用物理ID |
| UART0 | 0x10000000，IRQ10，字节访问 | 0x10000000，IRQ32，32位访问，偏移乘4 |
| PLIC S-context | 2*hart+1 | 2*hart |
| 时间源 | SBI TIME，10MHz | SBI TIME，4MHz |
| 100ms interval | 1000000 | 400000 |
| PTE | 常规Sv39 | 叶项预置A/D；非叶仍只置V |

UART/PLIC布局依据[Linux v6.12 JH7110设备树](https://github.com/torvalds/linux/blob/v6.12/arch/riscv/boot/dts/starfive/jh7110.dtsi)。
开发板继承U-Boot已工作的串口配置，不重设波特率，只在接收路径就绪后打开RX。

## 构建

仅在既定VMware Ubuntu中执行：

```bash
make -j2 PLATFORM=qemu
make -j2 PLATFORM=vf2
```

VF2产物是target/vf2/kernel/kernel-vf2.elf和kernel-vf2.bin。
两平台的用户程序、内核对象和生成头文件均独立。
VF2 bin前64字节为[RISC-V Image头](https://docs.kernel.org/arch/riscv/boot-image-header.html)，
text_offset=0x200000，image_size包含BSS，不把128MiB分配池写入文件。
使用现有OpenSBI/U-Boot及booti；不另行编译或刷写固件。

## 实板前置核实

用匹配板卡的DTB、U-Boot bdinfo和实际串口日志确认有效hart、TIME/HSM支持、
DRAM保留区和UART。内核管理窗口必须没有固件、运行中的U-Boot或DTB占用；
DTB应放在该窗口外。出现冲突时先调整平台常量与链接地址，再重新构建，
不要直接启动当前镜像。课程本阶段不要求解析DTB或引入HAL。

## 临时加载

按实际设备、分区与网络填写地址；先确认每次加载成功，失败后不得启动旧内存：

```text
setenv kernel_addr_r 0x40200000
setenv fdt_addr_r <经bdinfo确认位于管理窗口外的可用地址>
load mmc <设备:分区> ${kernel_addr_r} kernel-vf2.bin
load mmc <设备:分区> ${fdt_addr_r} <与板卡匹配的.dtb>
booti ${kernel_addr_r} - ${fdt_addr_r}
```

也可在实验室网络通过tftpboot临时加载。全过程不执行saveenv、不刷SPI、不写裸扇区。
命令依据[U-Boot booti说明](https://docs.u-boot.org/en/latest/usage/cmd/booti.html)。

## 已核实的硬件布局

板上DRAM为0x40000000～0x13fffffff。OpenSBI保留0x40000000～0x4003ffff，
U-Boot relocaddr=0xf7efd000、控制DTB=0xf76df9b0，均不占用内核管理窗口。
实际DTB的UART0为32位访问、reg-shift=2、IRQ32，timebase=4000000，
hart0被禁用，S-mode PLIC context为2*hart，与平台常量一致。
原始核实记录为E:\oslab\board-setup\logs\uboot-preflight.txt。

没有网线时，使用VMware内的tools/vf2_console.py，通过现有minicom/tmux会话传输文件；
不同时开启第二个串口读取进程。文件经SHA-256校验后另存到/boot/oslab，
不改写Ubuntu原有extlinux配置。

本板已验证的加载设备为mmc 1:3，DTB为
/dtbs/6.12.5-starfive/starfive/jh7110-starfive-visionfive-2-v1.3b.dtb，
加载地址为0x4a000000。每次load必须成功并核对字节数和CRC32，再执行booti。

tools/test-vf2.py根据本机board-setup/artifacts/board-images-manifest.json检查镜像；
默认在测试完成后保留实验内核运行。--boot-hart选项通过仅供测试的HSM加载器选择
启动hart；加载器位于0x49000000，位于内核管理窗口外，尚待本板实际验证。

## 复位限制

本次Lab5实板测试通过后，测试内核的SBI SRST冷重启停在
pmic_ops: cannot read pmic power register。自动测试默认不请求软重启；
--reset只供已验证复位功能的固件使用。遇到此状态，按住板边Reset键超过3秒后松开，
由串口脚本拦截U-Boot；不需要重新烧录TF卡。
该错误发生在测试通过后的固件复位路径，不撤销已经取得的运行结果。

## 验收范围

- Lab5已验证：hart1启动、四核各启动一次、内存与用户程序测试、并发节点池、逐核timer、共享tick及Sv39下UART/PLIC。
- 待补：其余启动hart的独立测试、Lab6各实板用例；未执行的检查不记为通过。

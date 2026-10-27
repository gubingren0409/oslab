# VisionFive 2 平台适配与验收

## 当前状态

课程lab5@4a0f4b72已正式要求VisionFive 2支持，本项目接入了该平台的源码和构建流程，
Lab6在此基础上扩展多进程。当前没有实板串口日志，VMware服务恢复前也没有双平台
构建结果，不能把已填写常量当作硬件验收完成。

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

## 必须补齐的实际结果

- 各有效hart仅启动一次，辅助入口不再次清BSS；
- 任意有效启动hart下UART、timer和共享tick正确；
- Sv39启用后UART/PLIC仍正常，内核栈guard有效；
- Lab5复制、堆栈、mmap与系统调用在真实开发板通过；
- Lab6多进程及睡眠唤醒在实体板重新验证。

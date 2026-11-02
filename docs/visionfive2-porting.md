# VisionFive 2 平台适配与验收

## 当前状态

课程lab5@4a0f4b72已正式要求VisionFive 2支持，Lab6在此基础上扩展多进程。
Lab5/Lab6已在既定VMware环境完成双平台构建。Lab5双核QEMU测试、
Lab6单/双核10项回归及默认程序演示均通过，共12次QEMU运行。
VF2 v1.3B、4GB完成13次实板运行：Lab5覆盖启动hart1～4；Lab6覆盖全部专项场景
及启动hart1～4。每次均验证四核、共享节点池、定时器与UART。
结构化记录与镜像校验值见docs/validation/vf2-results.json。

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
启动hart；加载器位于0x49000000，位于内核管理窗口外，已在本板验证选择hart1～4。
OpenSBI的固件冷启动hart仍为1，加载器通过HSM启动所选hart、停止原hart，再进入
未经改写的内核Image；没有刷写固件或保存U-Boot环境。

## 复现串口测试

当前工作区的board-setup/artifacts和TF卡/boot/oslab已准备固定测试镜像，
镜像来源提交记录在artifacts的manifest.json与结构化验收记录中。
在VMware Ubuntu中、开发板运行官方Ubuntu时执行：

```bash
cd /home/oslab/lab/oslab
python3 tools/vf2_console.py reboot-uboot
python3 tools/test-vf2.py suite
```

脚本逐次验证加载大小、CRC32、实际启动hart及所有通过标志。
看到PASS / HOLD RESET后按住板上Reset约4秒再松开；RUNNING时等待。
最后一次复位不拦截U-Boot，自动启动并登录官方Ubuntu，核对TF卡内镜像校验值。
如只需恢复同一批镜像的中断测试，可使用suite --resume。
本工具验证已经准备的固定镜像；修改内核后应在对应实验分支用原VMware环境重新构建，
更新本地manifest和TF卡镜像，不把Lab5测试程序与Lab6的系统调用ABI混合构建。

## 复位限制

本次Lab5实板测试通过后，测试内核的SBI SRST冷重启停在
pmic_ops: cannot read pmic power register。自动测试默认不请求软重启；
--reset只供已验证复位功能的固件使用。遇到此状态，按住板边Reset键超过3秒后松开，
由串口脚本拦截U-Boot；不需要重新烧录TF卡。
该错误发生在测试通过后的固件复位路径，不撤销已经取得的运行结果。

另一次物理复位在BootROM读卡时出现dwmci超时和BOOT fail，尚未进入实验内核；
再次Reset后恢复，随后Lab5 hart4完整通过。原始记录保留在完整串口日志中。

## 验收范围

- Lab5已验证：启动hart1～4、四核各启动一次、内存与用户程序测试、并发节点池、逐核timer、共享tick及Sv39下UART/PLIC。
- Lab6已验证：启动hart1～4、进程隔离、fork/wait、睡眠唤醒、槽耗尽与复用、睡眠锁、首进程退出、内存、四核timer及UART。

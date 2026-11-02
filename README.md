# Lab5 实验报告：用户虚拟内存、系统调用与双平台构建

- 学生：谷秉仁
- 实验分支：`lab-5`
- 课程框架：`course/lab5@4a0f4b72`
- 当前状态：VMware 双平台构建、QEMU 回归及 VF2 启动 hart 1～4 四核实板验收已通过。

## 实验要求

在 Lab4 首个用户进程上，实现 copyin/copyout/copyinstr、动态堆、多页栈缺页增长、
mmap 节点池与区间管理、独立用户地址空间复制和销毁，以及统一系统调用分发。
新增平台要求包括 QEMU/VisionFive 2 隔离构建、标准 RISC-V Image 头、非零 hart ID、
开发板 UART/PLIC、SBI TIME/HSM 和叶 PTE 的 A/D 位。

课程 PDF 含旧版空闲区链表示意；本实现以当前 README 与 Spec 为准，mmap 链记录
已分配区间，只有权限一致的相邻区间才合并。

## 环境与入口

沿用 VMware Ubuntu 22.04.5、riscv64-linux-gnu-gcc 11.4、Binutils 2.38、
QEMU 5.1、OpenSBI 0.7 和 GDB multiarch 12.1。
Windows `E:\oslab\oslab` 与 Ubuntu `/home/oslab/lab/oslab` 是同一份共享源码。
代码编辑在宿主机完成，编译、QEMU 和调试只在 VMware 中执行。

## 实现说明

- 用户复制按页检查 V/U/R 或 V/U/W；拒绝内核特殊映射，字符串必须在上限内遇到 NUL。
- 多页分配与页表复制复用页表事务：数据叶与新中间层分别标记，失败全部回收，
  不改写旧映射或软件保留 PTE。
- brk 先校验完整 64 位参数，成功后发布 heap_top；栈只在合法高地址范围发生
  load/store page fault 时向下补齐，并重试原指令。
- mmap 维护有序已分配区间，支持 first-fit、固定地址、合并与 munmap 裁剪拆分。
  解除前预检完整覆盖及拆分节点，空洞或节点不足时保持原状。
- 页表销毁只回收用户叶与页表页，trapframe 和 trampoline 的物理页由原所有者管理。
- 系统调用编号 0～7 为 helloworld、copyin、copyout、copyinstr、brk、mmap、
  munmap、printf；未知编号返回 -1，ecall 的 PC 只增加一次 4。
- QEMU 与 VF2 使用独立对象、用户镜像和生成头文件。保留助教工具链，兼容旧版 xxd。
- 初次入口清 BSS；HSM 次级入口不清 BSS。boot_hartid 标识实际启动 hart，
  每核使用独立栈；只有启动 hart 更新共享 tick 并创建用户进程。

## VisionFive 2 参数

依据课程引用的 Linux JH7110 设备树和 RISC-V Image 规范设置：
DRAM 起点 0x40000000，内核起点 0x40200000，管理窗口结束 0x48200000；
有效 hart 为 1～4，数组保留 5 个物理 ID 槽；UART0 为 0x10000000、IRQ32、
32 位访问、4 字节寄存器间隔；S-mode PLIC context 为 2*hart；
timebase 4MHz，100ms interval 为 400000；叶项预置 A/D，非叶只带 V。

开发板继承固件串口配置，产出带 64 字节 Image 头的 kernel-vf2.bin。
实板型号为 VisionFive 2 v1.3B、4GB，OpenSBI 1.2、U-Boot 2021.10。
已核实 bdinfo：U-Boot 重定位至 0xf7efd000，其控制 DTB 位于 0xf76df9b0；
固件保留 0x40000000～0x4003ffff，与内核管理窗口不重叠。
匹配板卡的 DTB 临时加载到 0x4a000000，位于管理窗口外。

## 可复现检查

在 VMware 中执行：

```bash
cd /home/oslab/lab/oslab
make clean
make -j2 build
python3 tools/test-labs.py lab5
make -j2 PLATFORM=vf2
python3 tools/check-vf2-image.py target/vf2/kernel/kernel-vf2
make -j2 PLATFORM=qemu
make debug
```

`make run PLATFORM=vf2` 和 `make debug PLATFORM=vf2` 应拒绝执行并提示使用 U-Boot
booti。禁止用 QEMU 运行 VF2 镜像。实板加载步骤见 docs/visionfive2-porting.md。

## 测试覆盖与当前结果

已实际通过的内存与用户态测试包括：
跨页用户复制、字符串长度与读写权限、特殊映射保护、独立页表复制、
内核页耗尽回滚、mmap 节点耗尽回滚、跨区间解除、堆伸缩、真实多页栈缺页、
非法系统调用和高 32 位非零的长度参数。

QEMU 双核输出 MEMORY_SELFTEST_PASS、MMAP_CONCURRENCY_PASS harts=2、LAB5_PASS，
两个 hart 的定时器均通过；共享 tick 只由实际启动 hart 更新。
默认 QEMU/VF2 镜像及切换平台后的增量构建均成功，VF2 ELF 入口、Image 头、BSS、
各核栈和分配池边界通过自动检查。

VF2 使用串口传输镜像并核对 SHA-256；U-Boot 每次加载后检查字节数和 CRC32，
随后执行 booti 0x40200000 - 0x4a000000。实际启动 hart=1，计算 hart 1～4 各启动一次。
实板输出 MEMORY_SELFTEST_PASS、MMAP_CONCURRENCY_PASS harts=4、LAB5_PASS，
四核均收到定时器中断；20 个共享 tick 的 time 差为 8015375，interval=400000。
输入 A 得到 UART_SELFTEST_PASS irq=32 hart=4，确认 Sv39 下串口接收和 PLIC 正常。

记录位于 E:\oslab\board-setup\logs\lab5-direct.log 和 lab5-direct.json。
另通过仅用于验收的 HSM 加载器，分别选择 hart2、3、4 作为内核启动 hart。
三种情况均通过 LAB5_PASS、并发 mmap 节点池、四核 timer、共享 tick 与 UART 检查。
结合直接 booti 的 hart1，共 4 次实板运行覆盖所有有效启动 hart；
各计算 hart 每次均仅启动一次。固件冷启动 hart 仍为 1，测试加载器在 RAM 中完成切换。
结构化结果与镜像校验值见 docs/validation/vf2-results.json。
此前镜像在 SBI SRST 冷重启时遇到固件 PMIC 读取失败，该问题与实验功能结果分别记录。

## 软件重启补充

已加入 VF2 专用的 I²C5/PMIC 恢复路径，正常运行和 panic 循环均接受串口 Ctrl+R。
Lab5 正式与 SELFTEST 镜像已重新构建，原 VMware 环境双核 QEMU SELFTEST 通过。
相同重启实现已在 Lab6 正式和 SELFTEST 镜像验证返回 U-Boot；Lab5 修复镜像的
连续实板验证尚待进行。SDIO BootROM 偶发读卡失败仍需处理，SPI 同版本固件更新
已完成只读备份与预检，尚未写入。
机制、命令和当前证据见 [软件重启说明](docs/vf2-software-reboot.md)及
[结构化记录](docs/validation/vf2-reboot-results.json)。

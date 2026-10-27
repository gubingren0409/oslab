# Lab5 实验报告：用户虚拟内存、系统调用与双平台构建

- 学生：谷秉仁
- 实验分支：`lab-5`
- 课程框架：`course/lab5@4a0f4b72`
- 当前状态：Spec 与代码已实现，VMware 运行验收待恢复；VisionFive 2 实板验收待接入设备。

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
真正启动前仍须用匹配板卡 DTB 与 bdinfo 核实固件、DRAM 保留区和 DTB 所在位置。
当前没有实板日志，不能把构建适配计为实板验收通过。

## 可复现检查

在 VMware 中执行：

```bash
cd /home/oslab/lab/oslab
make clean
make -j2 build
python3 tools/test-labs.py lab5
make -j2 PLATFORM=vf2
make -j2 PLATFORM=qemu
make debug
```

`make run PLATFORM=vf2` 和 `make debug PLATFORM=vf2` 应拒绝执行并提示使用 U-Boot
booti。禁止用 QEMU 运行 VF2 镜像。实板加载步骤见 docs/visionfive2-porting.md。

## 测试覆盖与当前结果

已加入可重复测试，尚未运行：
跨页用户复制、字符串长度与读写权限、特殊映射保护、独立页表复制、
内核页耗尽回滚、mmap 节点耗尽回滚、跨区间解除、堆伸缩、真实多页栈缺页、
非法系统调用和高 32 位非零的长度参数。

当前 Windows 的 VMware Authorization Service 停止，服务启动的 UAC 操作被取消，
虚拟机没有启动；尚未在其他环境替代编译。恢复服务并通过上述检查后再记录实际结果。

# Lab4：首个用户进程与用户态 Trap

## 发布依据

Lab4 依据课程分支 `course/lab4@11fd5f65bfc9fbc826a6388849aba9cbb3ed016a`
和 `course/master@6ce45ee9` 中的 Lab4 PPT 实现。课程远端没有 Tag 或 Release，但任务
README、Spec、框架、验收场景和 PPT 均已发布。

本分支从已完成的 `lab-3` 创建。课程分支中的 Lab1–3 文件仍含旧骨架，因此只导入 Lab4
新增的 proc、usertrap、trampoline、用户程序和 Spec，再手工接入既有 OpenSBI 双核、
Sv39、SBI timer、PLIC 和 UART 实现。

## 固定环境

构建、运行和调试均在 VMware Ubuntu 22.04.5 中完成：

```text
E:\oslab\oslab
  <-> /home/oslab/lab/oslab
  <-> /mnt/hgfs/oslab-host/oslab
```

工具链为 `riscv64-linux-gnu-gcc` 11.4、Binutils 2.38、QEMU 5.1、OpenSBI 0.7、
GDB multiarch 12.1 和 GNU Make 4.3。QEMU 使用 `virt`、2 hart、130 MiB RAM 与
`-bios default`。

## 核心实现

用户地址布局为：

| 地址 | 内容与权限 |
| ---: | --- |
| `0x0` | 零页未映射 |
| `0x1000` | initcode，RXU |
| `0x2000` | 初始 heap top |
| `0x3fffffd000` | 用户表为用户栈 RWU；内核表为独立内核栈 RW |
| `0x3fffffe000` | trapframe，RW、无 U；内核表不映射并作为 guard |
| `0x3ffffff000` | trampoline，RX、无 U，两张页表映射同一物理页 |

`proc_make_first()` 分配 trapframe、用户页表、代码页和用户栈页，设置
`epc=0x1000`、`sp=TRAPFRAME`、`ctx.ra=trap_user_return` 和内核栈顶。资源全部就绪
后先执行 `fence.i` 同步新写入的用户指令，再发布 `cpu.proc`，由 `swtch` 进入首次用户
返回路径。

用户 trap 闭环为：

```text
U-mode
  -> user_vector：保存全部用户寄存器，切内核页表和内核栈
  -> trap_user_handler：ecall / timer / UART / 用户异常分派
  -> trap_user_return：准备CSR和trapframe
  -> user_return：切用户页表、恢复寄存器、sret
  -> U-mode
```

`ecall` 保存的 PC 增加 4；`SYS_helloworld=0` 输出固定消息并返回 0，未知调用返回 -1。
timer cause 5 和 external cause 9 复用 Lab3 handler。其他用户异常输出上下文、撤下故障
进程并留在可处理中断的内核循环，不触发 panic。

内存接口同步升级为最新 Spec：`pmem_alloc` OOM 返回 NULL，`vm_mappages` 返回 0/-1、
拒绝非法权限和重复映射，并以事务标记回滚部分叶项和本次创建的中间页；新增
`vm_freewalk`。`V=0` 且非零的软件保留 PTE 不会被当作空槽覆盖。`kvm_init` 完整建立
trampoline 与 `KSTACK(0)` 后才发布内核页表。

## 验证摘要

| 验证项 | 结果 |
| --- | --- |
| `make clean && make build` | 通过，`-Wall -Werror` 无错误 |
| 用户 ELF | entry/main=`0x1000`，平坦镜像 44 bytes |
| trampoline | `0x80203000`，页对齐且不超过一页 |
| OpenSBI 首 hart 为 0 | 双 hart 启动，两次 hello |
| OpenSBI 首 hart 为 1 | 双 hart 启动，两次 hello |
| 用户态 timer | 命中 `scause=0x8000000000000005`，SPP=0 |
| UART | 普通字符、Delete、CR/LF 连续回显 |
| 未知 syscall | 返回 -1 后用户继续 |
| 零页访问 | cause 13、`stval=0`，停止用户进程且无 panic |
| 故障后的内核 | UART 仍可处理中断 |
| 内存 OOM | `vm_getpte` 和跨 2 MiB 部分映射均无泄漏并完整回滚 |
| 软件保留 PTE | 原值逐位保持，先前部分映射与临时页表完整回滚 |

默认场景的用户输出恰好为：

```text
proczero: hello world!
proczero: hello world!
```

完整设计、测试记录和复现命令见仓库根目录 [`README.md`](../README.md)。

## VisionFive 2 边界

S-mode trap、用户页表、trampoline 与 SBI TIME/HSM 可以复用。上板仍需根据 DTB 和固件
适配非零起始 hart ID、DRAM/保留区、JH7110 UART、PLIC context、镜像装载地址与启动介质；
当前 QEMU 结果不代替实体板验证。

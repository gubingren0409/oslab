# Lab4 实验报告：首个用户进程与用户态 Trap

- 学生：谷秉仁
- 实验分支：`lab-4`
- 课程框架：`course/lab4@11fd5f65`
- 完成状态：Spec、实现与验收项目均已完成

## 1. 发布依据与实验目标

课程仓库已经在 `master@6ce45ee9` 发布 Lab4 PPT，并在独立的 `lab4` 分支提供
README、Spec、代码框架和验收场景。远端仍没有 Git Tag 或 Gitee Release，但本次
PPT、任务分支和说明文档已经构成明确发布。

Lab4 的目标是在 Lab3 的双核、中断和 Sv39 基础上建立第一个用户进程 `proczero`：

1. 构造用户页表、代码页、用户栈、trapframe、内核栈和进程上下文。
2. 通过 `swtch` 首次进入用户返回路径，再以 `sret` 进入 U-mode。
3. 完成 `user_vector -> trap_user_handler -> trap_user_return -> user_return` 闭环。
4. 处理 `SYS_helloworld`，使用户程序连续两次打印
   `proczero: hello world!`，每次返回 0 且不重复执行 `ecall`。
5. 保持用户态期间的 timer 和 UART 中断可恢复。
6. 未知系统调用返回 -1；用户页错误停止故障进程，但不使内核 panic。
7. 按最新 Memory Spec 实现可恢复的页分配和原子映射失败回滚。

## 2. 实验环境

| 项目 | 配置 |
| --- | --- |
| 虚拟化 | VMware Workstation Pro 17 |
| 操作系统 | Ubuntu 22.04.5 LTS |
| 交叉编译器 | `riscv64-linux-gnu-gcc` 11.4.0 |
| Binutils | 2.38 |
| 模拟器 | QEMU 5.1.0，`virt`，2 hart，130 MiB RAM |
| 固件 | OpenSBI 0.7，`-bios default` |
| 调试器 | GDB multiarch 12.1 |
| Make | GNU Make 4.3 |

源码通过 VMware 共享文件夹实时映射，Windows 侧只负责编辑，所有编译、QEMU 和 GDB
操作均在 Ubuntu 中执行：

```text
Windows: E:\oslab\oslab
    <-> Ubuntu: /home/oslab/lab/oslab
    <-> 挂载点: /mnt/hgfs/oslab-host/oslab
```

## 3. 框架整合与兼容策略

课程 `lab4` 分支是累积的未完成框架，其中仍包含会破坏既有结果的旧实现，例如固定假设
hart0 首先启动、空的 Lab2 内存函数、未重新安排的 timer、错误的 UART IER 位和不完整的
kernel trap 分派。因此本分支从已验收的 `lab-3` 创建，只导入 Lab4 新增的进程、用户态、
trampoline、用户链接脚本和 Spec，再手工合并必要接口。

保留并回归验证了以下前序成果：

- OpenSBI 可以首先交付 hart0 或 hart1，首个进入者通过 CAS 取得共享初始化权；
- S-mode 只通过 SBI HSM/TIME 启动 hart 和设置 timer，不直接访问 CLINT；
- 两个 hart 都安装自己的页表、PLIC、trap 和 timer 环境；
- UART IER bit 0 为 RX，bit 1 为 TX，当前只打开 RX；
- timer 中断每次通过 SBI 重新安排，仅 hart0 更新共享 ticks；
- PLIC 使用 `volatile` MMIO，未知外设 IRQ 会 complete 并诊断，但不会 panic。

Spec 先作为独立提交完成，再进行代码实现。Spec 中也把“OpenSBI 必定先启动 hart0”修正为
与实际 QEMU/OpenSBI 行为一致的动态启动 hart 模型；无论谁负责共享初始化，只有 hart0
创建并运行 `proczero`。

## 4. 地址空间与进程构造

### 4.1 固定虚拟地址

| 区域 | 虚拟地址 | 用户页表权限 | 内核页表权限 |
| --- | ---: | --- | --- |
| 零页 | `0x00000000` | 未映射 | 按内核低地址策略未映射 |
| 用户代码 | `0x00001000` | RXU | 不作为用户映射使用 |
| 用户堆顶 | `0x00002000` | 尚未分配 | — |
| `KSTACK(0)` / 用户栈页 | `0x3fffffd000` | RWU，映射用户栈物理页 | RW，映射独立内核栈物理页 |
| `TRAPFRAME` | `0x3fffffe000` | RW、无 U | 未映射，形成内核栈保护页 |
| `TRAMPOLINE` | `0x3ffffff000` | RX、无 U | RX |

用户页表和内核页表在 `TRAMPOLINE` 映射同一物理页，使汇编可以在切换 `satp` 前后继续
取指。`TRAPFRAME` 对用户不可访问；内核切回自己的页表后通过 `proc.tf` 保存的恒等映射
物理地址访问同一页。

### 4.2 `proczero` 初始化

`proc_make_first()` 先在局部对象中完成全部资源构造，再一次性发布 PCB：

```text
分配 trapframe 用户物理页
  -> 建立只含 TRAMPOLINE/TRAPFRAME 的用户页表
  -> 分配用户栈页和代码页
  -> 映射 stack RWU、code RXU
  -> 复制 initcode 并执行 fence.i
  -> epc=USER_BASE、sp=TRAPFRAME
  -> ctx.ra=trap_user_return、ctx.sp=KSTACK(0)+PGSIZE
  -> 发布 cpu.proc
  -> swtch
```

任何分配或映射失败都会先释放本次页表页和用户物理页，再在发布进程前报告必要启动失败，
不会留下半初始化 PCB。

## 5. 用户态 Trap 闭环

### 5.1 首次进入与返回

`trap_user_return()` 先关闭全局中断，填入 trapframe 前五个内核字段：内核 `satp`、内核
栈顶、C trap 入口、用户 PC 和 hart ID。随后：

1. 把 `stvec` 指向 `TRAMPOLINE + (user_vector - trampoline)`；
2. 把保存的用户 PC 写入 `sepc`；
3. 清除 `sstatus.SPP`，设置 `SPIE`；
4. 通过 trampoline 高地址调用 `user_return(TRAPFRAME, MAKE_SATP(pgtbl))`；
5. 汇编切换到用户页表、恢复全部通用寄存器并执行 `sret`。

trapframe ABI 用编译期断言固定为 288 字节，其中 `a0` 偏移 112、`t6` 偏移 280，保证 C
结构体和汇编保存顺序一致。

### 5.2 用户态进入内核

用户 trap 到达 `user_vector` 后，汇编通过 `sscratch` 取得 `TRAPFRAME`，保存全部用户
寄存器，读取预置的内核上下文，切换内核页表和内核栈，再进入
`trap_user_handler()`。C 处理函数首先把 `stvec` 恢复为 `kernel_vector`，之后按完整
`scause` 分派：

| 原因 | 处理 |
| --- | --- |
| U-mode `ecall`，cause 8 | 保存的 `epc += 4`，按 `a7` 分派系统调用 |
| `SYS_helloworld=0` | 打印固定消息，`a0=0` |
| 未知系统调用 | `a0=-1` |
| S-mode timer interrupt，cause 5 | 复用 Lab3 timer handler，重新安排下一事件 |
| S-mode external interrupt，cause 9 | 复用 PLIC/UART handler |
| 其他用户异常 | 输出上下文，撤下当前进程，进入可处理中断的内核 `wfi` 循环 |

用户异常路径不返回故障指令，也不调用 kernel panic；另一个 hart 和内核 UART 中断仍可继续。

## 6. 内存失败恢复

Lab4 的最新 Spec 要求运行时 OOM 可恢复，而课程源码仍保留旧的 `void vm_mappages` 接口。
本实现按 Spec 完成升级：

- `pmem_alloc()` 在空闲链表耗尽时返回 `NULL`，不 panic；
- 空闲页以字节 1 填充，分配页完整清零；
- `vm_getpte()` 分配中间层失败时回收本次遍历创建的页表页；
- `vm_mappages()` 返回 0/-1，校验地址、溢出和权限，禁止覆盖已有映射；
- 对 `V=0` 且非零的软件保留 PTE 拒绝覆盖，并在失败后逐位保留原值；
- `vm_mappages()` 使用 PTE 的软件保留位标记事务中新建的中间页，成功时清除标记，失败时
  回滚本次叶项并只释放本次创建的中间页；
- `vm_unmappages()` 根据物理页所属区域选择正确的内核/用户空闲链表；
- `vm_freewalk()` 只回收页表页，不擅自释放叶项所指物理页；
- `kvm_init()` 在完整建立 UART、PLIC、内核、trampoline 和内核栈映射后才发布页表。

## 7. 构建系统

用户程序使用独立的 `user.ld` 从 `0x1000` 链接，`main` 显式放入入口 section。构建链为：

```text
initcode.c
  -> initcode.o
  -> initcode.elf
  -> objcopy: initcode.bin
  -> xxd -i: src/user/initcode.h
  -> proc.o
  -> kernel-qemu.elf
```

用户代码增加 `-msmall-data-limit=0`，避免在尚未初始化用户 `gp` 的阶段生成 small-data 相对
访问。Makefile 保留 `.DEFAULT_GOAL := build`、固定助教工具链、依赖文件和可靠的
`make clean -> make debug` 路径；生成的 `initcode.h` 已加入 `.gitignore`。

## 8. 验证结果

### 8.1 干净构建与 ELF 布局

在 VMware Ubuntu 中执行 `make clean && make build`，完整构建在 `-Wall -Werror` 下通过。
二进制检查结果：

```text
initcode.bin                         44 bytes
user ELF entry / main               0x1000
kernel _entry                       0x80200000
_trampoline / user_vector           0x80203000
user_return                         0x80203090
KERNEL_DATA                         0x80204000
```

用户镜像远小于一页，trampoline 页对齐且整个链接段占用一页。

### 8.2 默认双核与系统调用

多次运行中分别观察到 OpenSBI 报告 `Current Hart: 0` 和 `Current Hart: 1`。两种情况下
hart0、hart1 都各输出一次启动信息，用户输出始终恰好为：

```text
proczero: hello world!
proczero: hello world!
```

持续运行没有第三次输出或 panic，证明两个调用返回 0、`sepc` 正确前进 4 字节且用户程序
继续执行循环。

### 8.3 用户循环中的 timer trap

GDB 对 `trap_user_handler` 设置 `scause` 条件断点，实际命中：

```text
scause = 0x8000000000000005
sstatus = 0x8000000000006020   # SPP=0
sepc = 0x1018
sp = 0x3fffffe000
tp = 0
```

这证明中断来自 U-mode，用户 PC 和内核栈切换正确；处理后可以继续返回用户循环。

### 8.4 UART/PLIC

在用户循环期间连续输入 `abc<DEL>d<Enter>` 和 `xy<Enter>`：普通字符正常回显，Delete
产生 `\b \b`，回车产生 `\r\n`，第二组输入仍能继续处理。timer 与 UART 均通过用户
trap 往返，没有破坏用户 PC、寄存器或栈。

### 8.5 未知系统调用

临时用户入口先发起 syscall 123，只有结果等于 -1 时才调用一次 `helloworld`。实际只出现
一次固定消息并继续用户循环，验证未知号返回值。测试后已恢复默认的两次调用。

### 8.6 零页错误

临时用户入口在第一次系统调用后执行对地址 0 的读访问，实际结果：

```text
proczero: hello world!

user exception: load page fault
pid = 0, scause = 0x000000000000000d,
sepc = 0x0000000000001010, stval = 0x0000000000000000
```

第二次系统调用没有执行，输出中没有 panic；故障后继续输入 `z<Enter>` 仍能由内核 UART
路径回显。测试后已恢复默认用户入口。

### 8.7 OOM 与映射事务

临时启动自检实际耗尽内核页池，覆盖：非法 W-only 权限、重复映射、`vm_getpte` 第二级
分配失败，以及跨 2 MiB 边界时先建立一个叶项再 OOM 的部分映射。所有失败均保持原页表
不变；重新分配得到的页数与失败前一致，最终输出 `MEM_API_TEST_OK`。临时自检已撤下。

另一次事务自检在跨 2 MiB 的第二级路径放置 `V=0` 的软件保留 PTE，确认第一页面已发生的
映射被回滚、保留项逐位不变、临时中间页重新回到空闲链表，输出
`PTE_RESERVED_TEST_OK`。该测试钩子同样已从最终源码移除。

## 9. 完成情况

| 要求 | 状态 |
| --- | --- |
| 四个函数的 Spec 契约 | 完成并独立提交 |
| 用户代码独立链接与内嵌 | 完成 |
| `proczero` 页表、栈、trapframe、context | 完成 |
| trampoline 双页表映射与 ABI | 完成 |
| U-mode 首次进入与完整 trap 往返 | 完成 |
| 两次 `SYS_helloworld`、返回 0、不重放 | 完成并验证 |
| 用户态 timer 与 UART 中断恢复 | 完成并验证 |
| 未知 syscall 返回 -1 | 完成并验证 |
| 零页 fault 停止用户进程且内核不 panic | 完成并验证 |
| OOM/重复映射/部分映射失败回滚 | 完成并验证 |
| 动态启动 hart 与 Lab1–3 兼容 | 完成并验证 |
| 根目录 README 实验报告 | 完成 |

## 10. 复现命令

```bash
cd /home/oslab/lab/oslab
make clean && make build
make run
```

退出 QEMU：先按 `Ctrl-a`，再按 `x`。

GDB 调试：

```bash
# 终端一
make debug

# 终端二
gdb-multiarch -q -nx target/kernel/kernel-qemu.elf
(gdb) target remote :26000
(gdb) break trap_user_handler
(gdb) continue
```

## 11. VisionFive 2 适配边界

当前用户页表、trampoline、S-mode trap、SBI TIME/HSM 和不固定首个启动 hart 的结构可继续
复用于 VisionFive 2。真正上板仍需按助教最终固件和 DTB 完成：

- 将固件 hart ID 映射为紧凑的内核 CPU 索引；
- 从 DTB 获取 DRAM、保留区、UART、PLIC 和 timebase，而不是使用 QEMU 常量；
- 适配 JH7110 的 DesignWare APB UART 寄存器宽度和间隔；
- 确定 U-Boot/OpenSBI 装载地址、镜像格式和页表物理范围；
- 在实体板上重新验证 trampoline、timer、PLIC、串口和用户态往返。

因此当前结果保留了可移植的 S-mode/SBI 边界，但不把 QEMU 验收等同于开发板适配完成。

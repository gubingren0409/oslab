# Lab2：物理内存与内核虚拟内存

## 发布依据与实现范围

Lab2 已按课程正式发布内容实现。采用的两项发布依据为：

- 课程框架分支 `course/lab2`，固定提交 `79b9ad32`；
- 课程 `master` 固定提交 `0c3849a` 中的 `lab-ppt/拔尖班-lab-2.pptx`。

课程仓库还存在 `course/lab3`（`00970e6`），但当前没有与 Lab2 同等的 PPT、标签或发布说明，
因此将其视为预发布框架。本次没有导入或实现 Lab3，避免把未正式布置的后续内容混入 Lab2。

## 固定实验环境

Lab2 的唯一构建、运行和调试环境是 VMware 中的 Ubuntu 22.04.5，而不是 Windows 或 WSL。
宿主机目录通过 VMware 共享文件夹映射到虚拟机，路径关系如下：

```text
Windows: E:\oslab\oslab
    <-> VMware 逻辑路径: /home/oslab/lab/oslab
    <-> VMware 实际挂载: /mnt/hgfs/oslab-host/oslab
```

实际使用的工具版本为：

| 工具 | 版本或配置 |
| --- | --- |
| 操作系统 | VMware Ubuntu 22.04.5 |
| 交叉编译器 | `riscv64-linux-gnu-gcc` 11.4.0 |
| QEMU | 5.1.0，`virt`、2 hart、130 MiB RAM |
| 固件 | OpenSBI 0.7，QEMU `-bios default` |
| 调试器 | GDB multiarch 12.1 |

这里有意使用课程环境中的 `riscv64-linux-gnu-` 工具链，未改成
`riscv64-unknown-elf-`。所有 `make build`、QEMU 和 GDB 命令都必须在上述 VMware
路径内执行；Windows 仅用于编辑共享目录中的同一份源码。

## 启动路径

```text
QEMU virt reset
  -> OpenSBI（M-mode，0x80000000 起）
  -> _entry（S-mode，ELF 入口 0x80200000）
  -> start（把 OpenSBI 传入 a0 的 hart ID 保存到 tp）
  -> main
  -> 首个 hart 用原子 CAS 当选启动核并完成全局初始化
  -> 通过 SBI HSM 启动另一个 hart
  -> 每个 hart 分别执行 kvm_inithart
```

OpenSBI 决定最初进入内核的 hart，不能假设它必然是 hart 0。因此 `main` 使用原子比较交换
选出唯一启动核，并在发布初始化完成状态前执行内存屏障。不同运行中实测过 hart 0
和 hart 1 分别成为启动核；当 hart 1 当选时，它会通过 SBI HSM 启动 hart 0。两个 hart 都调用
`kvm_inithart()`，分别
写入 `satp` 并执行 `sfence.vma`，不存在只让启动核启用页表的遗漏。

与 Lab1 的 `-bios none` M-mode 入口不同，Lab2 从 OpenSBI 进入 S-mode。内核不再自行配置
M-mode CSR、PMP 或执行 `mret`；这也是保留 Lab1 成果但不直接沿用其入口代码的原因。

## 物理内存管理

物理内存以 4 KiB 页为基本单位。QEMU RAM 为
`0x80000000`～`0x88200000`，OpenSBI 占用低端区域，内核从 `0x80200000` 链接。
链接脚本将内核镜像后的页对齐地址记为 `ALLOC_BEGIN`，可分配区终点为
`ALLOC_END=0x88200000`。

可分配区域被拆成两个互不混用的空闲页链表：

- 从 `ALLOC_BEGIN` 开始的前 1024 页属于内核池，用于内核对象和页表；
- 其余页面属于用户池；
- 两个池分别用自旋锁保护链表头和可分配页计数，可由两个 hart 并发访问；
- 分配从对应空闲链表取出一页，并在返回前清零整页；
- 释放时检查 4 KiB 对齐、所属池的地址范围和重复释放，再把页面放回链表；
- 空闲页耗尽时立即 `panic`，不会返回一个无效地址继续运行。

专项临时自测先在建立内核页表之前并发取尽并归还全部 1024 个内核页，从而避免页表自身
已经占用若干内核页而使测试结论失真。测试同时覆盖了分配页全页清零，以及用户页的地址
范围、释放后重新分配和再次清零。

## Sv39 与内核页表

实现使用 4 KiB 页和 Sv39 三级页表。`vm_getpte` 按 VPN[2]、VPN[1]、VPN[0] 逐级查找；
在 `alloc=true` 且中间页表不存在时，从内核页池申请并清零新页表。遇到本应为中间页表却
带 R/W/X 的大页叶子项时会停止，而不会把它误当作下一级页表。

`vm_mappages` 与 `vm_unmappages` 的边界语义为：

- VA、PA 必须页对齐，长度必须大于 0；
- 检查 `va + len <= VA_MAX` 时避免整数溢出；
- 长度不必是页大小的整数倍，覆盖 `[va, va + len)` 接触到的全部页面；
- 允许对已有叶子 PTE 重新映射，以便替换物理页或权限；
- 解除映射时清除叶子 PTE；`freeit=true` 时把对应物理页归还用户页池；
- 本实验不回收已经创建的中间页表页。

按照课程 PPT，最终内核恒等映射及权限矩阵如下：

| 区域 | VA/PA 关系 | 权限 | 说明 |
| --- | --- | --- | --- |
| UART | VA = PA | R/W | 映射一个 4 KiB 页 |
| CLINT | 不映射 | — | 由 OpenSBI 管理，不由 S-mode 内核直接访问 |
| PLIC | VA = PA | R/W | 映射完整 PLIC MMIO 区域 |
| 内核代码 | VA = PA | R/X | `KERNEL_BASE` 到 `KERNEL_DATA` |
| 内核数据 | VA = PA | R/W | `KERNEL_DATA` 到可分配内存 |
| 动态 RAM | VA = PA | R/W | 包含内核页池和用户页池 |

专项页表临时自测覆盖了普通映射、重新映射和解除映射，`PGSIZE+1` 长度跨两个页面且跨越
2 MiB 页表边界，`VA_MAX-PGSIZE` 的最高合法页面，以及只带 `PTE_W` 的课程结构测试。
RISC-V 规范中 W-only 叶子并不是可实际访问的合法权限组合；该用例只验证页表建立、遍历和
PTE 位结构，不代表内核会通过该映射执行真实访存。自测还逐项核对了上述内核映射权限，
并确认 CLINT 未映射。

## 专项临时自测

专项测试代码只用于验收，完成后已从生产源码移除。一次完整运行的原始关键输出为：

```text
SELFTEST pmem-kernel parallel/full/zero: PASS
SELFTEST pmem-user range/free/zero: PASS
SELFTEST Sv39 map/remap/unmap/boundaries: PASS
SELFTEST kernel permission matrix: PASS
SELFTEST ALL LAB2 TESTS: PASS
cpu 1 is booting with OpenSBI!
cpu 0 is booting with OpenSBI!
```

五组 PASS 覆盖：

1. 双核并发分配并释放全部 1024 个内核页，且验证每个页面全页清零；
2. 用户页地址范围、释放、重新分配及全页清零；
3. 页表 map、remap、unmap 和回收用户页；
4. `PGSIZE+1` 跨 2 MiB 边界、`VA_MAX-PGSIZE` 与 W-only PTE 结构；
5. UART、PLIC、代码、数据、动态 RAM 的权限矩阵及 CLINT 未映射。

这五行 PASS 记录的是临时专项内核的结果，不会作为额外测试分支留在最终启动路径中。

## 最终验收记录

| 检查 | 结果 |
| --- | --- |
| 课程框架与 PPT 发布依据 | 已确认：`course/lab2@79b9ad32`、`master@0c3849a` |
| 专项临时 pmem/Sv39/KVM 自测 | 通过，五行 `SELFTEST ... PASS` |
| 专项临时双核启动 | 通过，实测 hart 1 初始化、hart 0 由 SBI HSM 启动 |
| 最终生产源码 `make clean && make build` | 通过，`-Wall -Werror` 无错误 |
| 最终生产 ELF 入口 `0x80200000` | 通过，`readelf -h` 实测值为 `0x80200000` |
| 最终生产双核 QEMU 启动 | 通过，CPU 0 和 CPU 1 均输出 OpenSBI 启动信息 |
| 最终生产 GDB 命中 `main` | 通过，`pc=0x80200020`、`tp=0`、`a0=0` |

上表的最终四项均在移除临时测试代码后，使用 VMware 中的生产工作树重新执行。
对应命令为：

```bash
cd /home/oslab/lab/oslab
make clean && make build
riscv64-linux-gnu-readelf -h target/kernel/kernel-qemu.elf
timeout 10s make run
```

GDB 验证时，一个终端执行 `make debug`，另一个终端执行：

```bash
gdb-multiarch -q -nx -x .gdbinit
(gdb) break main
(gdb) continue
```

## 当前边界与后续移植

Lab3 仍按预发布内容处理，本次没有导入其 trap、时钟中断或设备驱动任务。

当前 Lab2 页表面向 QEMU `virt` 的固定地址布局。VisionFive 2 同样可以由 OpenSBI 把内核交给
S-mode，但板卡适配尚未完成：仍需解析或采用正确 DTB，按板卡实际设备地址重建 UART/PLIC
等映射，并处理固件提供的 hart ID 与内核逻辑 CPU 编号之间的映射。不能把“已使用 OpenSBI
启动”和“完成 VisionFive 2 板卡适配”视为同一件事。

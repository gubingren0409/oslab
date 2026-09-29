# Lab3：S-mode 中断、异常、UART 与时钟

## 发布依据

Lab3 依据课程分支 `course/lab3@00970e6549045bbc3a3f8bbacae09349f1d5ca36`
实现。该分支包含完整任务说明和代码框架；课程 `master` 当前尚无 Lab3 PPT、标签或
Release。

本分支从已完成的 `lab-2` 创建，只导入 Lab3 新增或必要变更的 trap、PLIC、timer、
SBI TIME 文件，保留前序实验的双核启动、UART/printf、自旋锁、物理内存和 Sv39 页表。

## 固定环境

构建、运行和调试环境为 VMware Ubuntu 22.04.5：

```text
E:\oslab\oslab
  <-> /home/oslab/lab/oslab
  <-> /mnt/hgfs/oslab-host/oslab
```

| 工具 | 版本或配置 |
| --- | --- |
| GCC | `riscv64-linux-gnu-gcc` 11.4.0 |
| QEMU | 5.1.0，`virt`，2 hart，130 MiB |
| 固件 | OpenSBI 0.7，`-bios default` |
| GDB | multiarch 12.1 |
| Make | 4.3 |

## 初始化与 trap 流程

唯一启动 hart 先完成 `print_init -> pmem_init -> kvm_init -> trap_kernel_init`，发布
初始化状态后通过 SBI HSM 启动另一 hart。每个 hart 再依次执行：

```text
kvm_inithart
  -> intr_off
  -> 安装 stvec、配置 PLIC、设置 SEIE
  -> SBI 设置首次 timer、设置 STIE
  -> intr_on
  -> wfi
```

`kernel_vector` 保存除 `x0` 外所有通用寄存器，调用 `trap_kernel_handler()`，恢复寄存器后
执行 `sret`。C 处理函数按完整 cause 编号分发：cause 5 处理 S-mode timer，cause 9
处理 S-mode external interrupt；其他中断和异常输出 `scause/sepc/stval` 并停止。

## 时钟

- `timer_create()` 清零共享 ticks 并初始化自旋锁。
- `timer_rearm()` 调用 `sbi_set_timer(r_time() + INTERVAL)` 并检查 SBI 错误码。
- 每个 hart 都设置首次事件，并在每次 timer interrupt 后分别重新设置下一次事件。
- 仅 hart 0 调用 `timer_update()`，避免双核使共享时钟计数翻倍。
- 更新和读取 ticks 都由同一自旋锁保护。
- 内核不映射或直接访问 CLINT。

## PLIC 与 UART

- UART IRQ 10 的优先级设为 1，每个 hart 的 S-mode context 打开 IRQ 10，threshold 为 0。
- PLIC MMIO 使用 `volatile`，处理顺序为 claim、UART FIFO 处理、complete。
- UART 只打开 IER bit 0 的 RX 中断；同步发送不打开 TX-empty 中断。
- `CR/LF` 统一换行，Backspace 和 Delete 使用 `\b \b` 擦除，行首退格被忽略。

## 验证结果

| 验证项 | 结果 |
| --- | --- |
| `make clean && make build` | 通过，`-Wall -Werror` 无错误 |
| OpenSBI 选择 hart 0 启动 | 双 hart 均进入内核 |
| OpenSBI 选择 hart 1 启动 | 双 hart 均进入内核 |
| 两 hart timer handler | 均命中 cause 5，`sie=0x220` |
| 共享 ticks | 仅 `tp=0` 更新，数值单调递增 |
| `INTERVAL` 四倍测试 | `time` 差值约变为四倍 |
| UART 连续输入 | 普通字符、CR、LF、Backspace、Delete 均正确 |
| trap 上下文恢复 | `tp`、`s7`、`t6` 标记值保持不变 |
| 同步异常 | 输出完整上下文并进入 `panic` |

定时中断的实际 GDB 观测值：

```text
tp=0 scause=0x8000000000000005 sie=0x220 stvec=0x80200020
tp=1 scause=0x8000000000000005 sie=0x220 stvec=0x80200020
```

UART 输入 `abc<DEL>d<Enter>` 显示为 `abd` 后换行；继续输入
`<DEL>xy<Enter>` 显示为 `xy`，证明 PLIC complete 后仍能接收后续中断。

异常路径通过实际触发 Instruction page fault 验证：

```text
unexpected exception: Instruction page fault
scause = 0x000000000000000c, sepc = 0x0000000000000000, stval = 0x0000000000000000
panic! trap_kernel_handler
```

测试用 ticks 输出和修改后的 `INTERVAL` 均已撤销，最终源码保持安静的生产启动路径。
完整实验报告与复现命令见仓库根目录 [`README.md`](../README.md)。

## 开发板边界

OpenSBI TIME/HSM 与 S-mode trap 结构可以继续用于 VisionFive 2。当前 UART、PLIC、内存
地址仍是 QEMU `virt` 配置，上板时需根据 DTB 和板卡固件重新确定设备地址、中断号、PLIC
context、串口参数、hart 映射和镜像装载方式。

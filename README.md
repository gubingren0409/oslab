# Lab3 实验报告：中断、异常、UART 与时钟

- 学生：谷秉仁
- 实验分支：`lab-3`
- 课程框架：`course/lab3@00970e65`
- 完成状态：必做内容与补充测试均已完成

## 1. 发布依据与实验目标

课程发布仓库已经存在独立的 `lab3` 分支，最新提交为 `00970e65`。该分支从
Lab2 框架继续演进，给出了 trap、PLIC、UART 与 SBI TIME 的实验接口。课程
`master` 当前没有 Lab3 PPT、标签或 Release，因此本实现以该任务分支中的 README、
源码结构和待实现项为依据。

本实验完成以下目标：

1. 为 S-mode 内核建立完整的 trap 入口、上下文保存与恢复流程。
2. 根据 `scause` 区分中断与异常，处理 S-mode 时钟中断和外设中断。
3. 通过 OpenSBI TIME 扩展为每个 hart 设置定时事件，并维护并发安全的共享 ticks。
4. 通过 PLIC 识别 UART IRQ，实现普通字符、换行、Backspace 和 Delete 回显。
5. 对未知中断和异常输出 `scause`、`sepc`、`stval` 后进入 `panic`。

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

源码只保留一份，由 VMware 共享文件夹实时映射：

```text
Windows: E:\oslab\oslab
    <-> VMware 逻辑路径: /home/oslab/lab/oslab
    <-> VMware 实际挂载: /mnt/hgfs/oslab-host/oslab
```

编辑可以在 Windows 侧完成，但交叉编译、QEMU 运行和 GDB 调试均在上述 VMware
Ubuntu 环境中执行。

## 3. 框架整合与前序实验兼容

`lab-3` 从已完成的 `lab-2` 创建，仅选择性导入 Lab3 新增的 trap、PLIC、timer 和
SBI TIME 框架。没有整树覆盖课程分支，因此保留了已经验证的以下成果：

- Lab1 的双核启动、UART、`printf` 和自旋锁；
- Lab2 的物理页分配、Sv39 内核页表和 OpenSBI S-mode 启动路径；
- 启动 hart 的原子选举。OpenSBI 最初交给内核的 hart 可能是 0，也可能是 1，不能固定
  假设 hart 0 先启动；
- QEMU `-bios default` 配置。S-mode 内核不直接访问或映射 CLINT。

同时修正了 16550 UART IER 位定义：bit 0 是接收数据可用中断，bit 1 是发送保持寄存器
空中断。当前同步发送逻辑只打开 RX 中断，避免空闲 UART 持续触发 TX-empty 中断。

## 4. 实现说明

### 4.1 初始化顺序

整体启动与中断初始化流程如下：

```text
OpenSBI
  -> _entry
  -> start（将 a0 中的 hart ID 写入 tp）
  -> main
      -> 首个 hart 通过原子 CAS 成为启动 hart
          -> print_init
          -> pmem_init
          -> kvm_init
          -> trap_kernel_init（PLIC 全局配置、共享 timer 创建）
          -> 发布全局初始化完成状态
          -> SBI HSM 启动另一个 hart
      -> 每个 hart
          -> kvm_inithart
          -> intr_off
          -> trap_kernel_inithart
          -> timer_init
          -> intr_on
          -> wfi
```

每个 hart 先显式关闭全局中断，再安装 `stvec` 并配置 PLIC S-mode context 和
`sie.SEIE`。随后 `timer_init()` 安排一个未来的定时事件并设置 `sie.STIE`，所有入口、
中断控制器和使能位就绪后才打开 `sstatus.SIE`。这样不存在 timer 在正确 trap 入口安装前
到达的窗口。

### 4.2 trap 入口与分发

`kernel_vector` 为除 `x0` 外的 31 个通用寄存器保留 256 字节栈空间，调用 C 语言
处理函数后逐一恢复寄存器，并通过 `sret` 返回。`tp` 保存 hart ID，也包含在恢复范围内。

`trap_kernel_handler()` 使用 `scause` 最高位判断中断或同步异常，并使用其余完整位作为
cause 编号，避免只保留低 4 位造成错误分类。当前支持：

| cause | 类型 | 处理 |
| --- | --- | --- |
| 5 | S-mode timer interrupt | 重新安排定时事件，hart 0 更新共享 ticks |
| 9 | S-mode external interrupt | 进入 PLIC claim/complete 流程 |
| 其他中断 | 未支持 | 输出寄存器上下文并 `panic` |
| 同步异常 | Lab3 不恢复 | 输出异常名称及寄存器上下文并 `panic` |

异常和中断名称表都带边界检查，未知的大编号不会越界访问字符串数组。

### 4.3 OpenSBI 时钟与共享 ticks

内核通过 SBI TIME 扩展调用：

```c
sbi_set_timer(r_time() + INTERVAL);
```

首次事件和每次中断后的下一次事件都检查 SBI 返回值，失败时输出 hart ID 与错误码并
`panic`。定时事件属于各 hart，因此两个 hart 都必须在各自的时钟中断中重新设置下一次
事件；共享 ticks 则只由 hart 0 更新，避免双核下计数速度翻倍。

`timer_create()` 初始化 ticks 和自旋锁，`timer_update()` 在锁内递增，
`timer_get_ticks()` 在锁内取得快照。这样后续调度器或系统调用读取 ticks 时不会与更新并发。

### 4.4 PLIC 与 UART

PLIC MMIO 访问使用 `volatile` 指针。全局初始化把 UART IRQ 10 的优先级设为 1；每个
hart 分别打开对应 S-mode context 的 IRQ 10，并把 threshold 设为 0。外设中断处理遵循：

```text
PLIC claim
  -> irq == 0：返回
  -> irq == 10：uart_intr -> PLIC complete
  -> 其他 irq：先 complete，再输出错误并 panic
```

UART 中断函数会排空接收 FIFO，并按以下规则回显：

- 普通字符直接回显并增加当前列计数；
- `CR` 和 `LF` 统一输出换行并清零列计数；
- Backspace 与 Delete 在行内输出 `\b \b`，擦除一个字符；
- 行首的 Backspace/Delete 被忽略，不会退到上一行。

## 5. 测试与结果

### 5.1 构建与双核启动

在 VMware 中执行：

```bash
cd /home/oslab/lab/oslab
make clean && make build
timeout --signal=INT 6s make run
```

完整构建在 `-Wall -Werror` 下通过。多次启动分别观察到 OpenSBI 将 hart 0 或 hart 1
作为 Current Hart，两个方向都能完成启动 hart 选举并得到：

```text
cpu 0 is booting with OpenSBI!
cpu 1 is booting with OpenSBI!
```

持续运行期间没有出现 panic、重复响应同一次 timer 或 UART TX 中断风暴。

### 5.2 两个 hart 的时钟中断

GDB 在 `timer_interrupt_handler` 连续命中四次的结果为：

```text
TIMER_HIT_1 tp=0 scause=0x8000000000000005 sie=0x220 stvec=0x80200020
TIMER_HIT_2 tp=1 scause=0x8000000000000005 sie=0x220 stvec=0x80200020
TIMER_HIT_3 tp=0 scause=0x8000000000000005 sie=0x220 stvec=0x80200020
TIMER_HIT_4 tp=1 scause=0x8000000000000005 sie=0x220 stvec=0x80200020
```

这说明两个 hart 都会周期性收到 cause 5 的 S-mode timer interrupt，`sie=0x220`
同时包含 STIE(bit 5) 和 SEIE(bit 9)，并且两个 hart 使用同一 `kernel_vector`。

在 `timer_update()` 入口连续观察到 ticks 为 `1239、1240、1241、1242、1243`，每次
`tp` 都为 0，证明共享计数单调递增且没有被 hart 1 重复累加。临时加入前十次滴答输出后，
实际结果为：

```text
ticks = 1
ticks = 2
ticks = 3
ticks = 4
ticks = 5
ticks = 6
ticks = 7
ticks = 8
ticks = 9
ticks = 10
```

该输出代码只用于验收，随后已从最终源码移除。

### 5.3 INTERVAL 快慢测试

默认 `INTERVAL=1000000` 时，相邻 hart 0 更新点的 `time` CSR 差值为：

```text
1026895, 1026851, 1012232, 1026276
```

临时改为 `INTERVAL=4000000` 后，差值变为：

```text
4031607, 4010345
```

中断周期随 `INTERVAL` 约增大四倍。测试后已恢复 `INTERVAL=1000000` 并重新构建。

### 5.4 UART/PLIC 交互测试

在 QEMU 终端逐次输入：

| 输入序列 | 实际显示 | 结论 |
| --- | --- | --- |
| `abc<DEL>d<Enter>` | `abd` 后换行 | Delete 擦除与 CR 换行正确 |
| `<DEL>xy<Enter>` | `xy` 后换行 | 行首 Delete 被安全忽略 |
| `q<LF>` | `q` 后换行 | LF 换行正确 |

三组输入均在同一次内核运行中完成，说明 PLIC complete 后可以继续接收下一次 UART 中断。

### 5.5 上下文恢复测试

在 hart 1 进入 `wfi` 前，使用 GDB 将两个寄存器设置为标记值，等待一次真实时钟中断并
在 `sret` 返回点检查：

```text
REG_MARKERS_SET tp=1
REGISTERS_AFTER_TRAP tp=1 s7=0x1122334455667788 t6=0x8877665544332211
```

被调用者保存寄存器 `s7`、临时寄存器 `t6` 和保存 hart ID 的 `tp` 均保持不变。

### 5.6 异常与 panic 测试

在 trap 初始化完成后，通过 GDB 把 hart 1 的下一条执行地址临时设为 0，实际触发
Instruction page fault。内核输出：

```text
unexpected exception: Instruction page fault
scause = 0x000000000000000c, sepc = 0x0000000000000000, stval = 0x0000000000000000
panic! trap_kernel_handler
```

这验证了同步异常分类、64 位寄存器输出和失败停止路径。

## 6. 完成情况

| 要求 | 状态 |
| --- | --- |
| trap 全局与每 hart 初始化 | 完成 |
| `kernel_vector` 上下文保存、恢复与 `sret` | 完成并验证 |
| S-mode timer cause 5 分发 | 完成并验证双 hart 周期触发 |
| SBI TIME 首次设置与每次重新设置 | 完成 |
| 共享 ticks 创建、更新与读取 | 完成并验证单调性 |
| 仅 hart 0 更新共享 ticks | 完成并验证 |
| PLIC S-mode claim/complete | 完成 |
| UART IRQ 10 识别与回显 | 完成并验证 |
| 换行、Backspace、Delete | 完成并验证 |
| 未知 trap 的上下文输出与 panic | 完成并验证 |
| Lab1/Lab2 双核启动与页表兼容 | 完成回归验证 |
| 根目录 README 实验报告 | 完成 |

## 7. 复现命令

普通构建与运行：

```bash
cd /home/oslab/lab/oslab
make clean && make build
make run
```

退出 QEMU：先按 `Ctrl-a`，再按 `x`。

GDB 调试：

```bash
# 终端一
cd /home/oslab/lab/oslab
make debug

# 终端二
cd /home/oslab/lab/oslab
gdb-multiarch -q -nx -x .gdbinit
(gdb) break timer_interrupt_handler
(gdb) continue
(gdb) p/x $scause
(gdb) p/x $sie
(gdb) p/x $stvec
```

## 8. VisionFive 2 适配边界

当前实现避免直接访问 CLINT，统一通过 OpenSBI TIME 和 HSM 扩展完成定时与 hart 启动；
trap 分发、SBI timer 和共享 ticks 逻辑可继续用于 VisionFive 2。硬件相关部分仍按 QEMU
`virt` 固定地址配置，真正上板前还需要：

- 从 DTB 获取实际 hart、内存、UART、PLIC 地址与中断号；
- 建立固件 hart ID 到内核逻辑 CPU 编号的映射；
- 按开发板串口时钟和寄存器参数初始化 UART；
- 按板卡 PLIC context 布局配置每个 hart；
- 补充镜像装载、启动介质和真实硬件中断测试。

因此本实验已经保留可移植的 OpenSBI/S-mode 边界，但不把 QEMU 验证等同于开发板适配完成。

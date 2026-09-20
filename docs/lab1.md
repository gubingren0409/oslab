# Lab1：机器启动

## 完成结果

Lab1 已完成以下核心目标：

- QEMU `virt` 双 hart 从 M-mode 启动并进入 S-mode 的 `main`；
- UART 格式化输出；
- 可嵌套关中断与多核自旋锁；
- 使用自旋锁保护整次 `printf`，避免字符级交错；
- VMware 中的构建、运行和 GDB 调试验证。

最终启动输出为：

```text
cpu 0 is booting!
cpu 1 is booting!
```

## 启动路径

```text
QEMU reset ROM
  -> _entry (M-mode，为每个 hart 设置独立栈)
  -> start (保存 hart ID，准备 M-mode CSR)
  -> mret (M-mode -> S-mode)
  -> main
```

`start` 完成的工作：

1. 关闭分页，保持物理地址访问。
2. 将 `mhartid` 保存到 `tp`，供 `mycpuid()` 和锁使用。
3. 设置 `mstatus.MPP=S` 和 `mepc=main`。
4. 将异常和中断委托到 S-mode，并允许读取基础计数器。
5. 配置 PMP，使 S-mode 可以访问 QEMU 的物理内存和 MMIO。
6. 执行 `mret` 进入 `main`。

CPU 0 初始化 UART 和打印锁，输出完成后通过内存屏障发布 `started`；CPU 1 等待该标志，
因此不会在公共资源初始化前调用 `printf`。两个 CPU 完成输出后进入 `wfi`。

## 格式化输出

`printf` 在一次调用开始时获取 `print_lk`，直到整个格式字符串输出完毕才释放。支持：

| 格式 | 含义 |
| --- | --- |
| `%d` | 32 位有符号十进制，包含 `INT32_MIN` |
| `%p` | 32 位无符号十六进制 |
| `%x` | 带 `0x` 前缀的 64 位十六进制 |
| `%c` | 字符 |
| `%s` | 字符串，空指针显示为 `(null)` |
| `%%` | 百分号 |

未知格式按 `%<字符>` 原样显示，末尾孤立 `%` 也不会丢失。`panic` 直接使用 UART 输出，
避免在已经持有打印锁时再次获取同一把锁。

临时格式测试（测试代码未保留在最终内核）结果：

```text
FORMAT d=-2147483648 p=2a x=0x0000000000001234 c=Z s=ok null=(null) percent=% unknown=%q
TRAIL=%
```

## 自旋锁

加锁路径先通过 `push_off` 关闭当前 CPU 的中断，再用原子交换竞争 `locked`；成功后执行
内存屏障并记录持有者。解锁路径验证持有者、执行释放屏障、原子清锁，最后用 `pop_off`
恢复原来的中断状态。

反汇编确认锁路径生成了 RISC-V `amoswap.w.aq`、`fence` 和原子释放指令。

## 并发实验

### 并行累加

两个 CPU 各执行 1,000,000 次无锁 `sum++`，实测两次结果分别为：

```text
UNLOCKED sum=1108750 expected=2000000
UNLOCKED sum=1136845 expected=2000000
```

丢失更新说明 `volatile` 只能阻止编译器省略访问，不能让“读取—加一—写回”成为原子操作。

随后让两个 CPU 各执行 10,000 次由自旋锁保护的累加，结果为：

```text
LOCK sum=20000 expected=20000
```

每次加一都加锁属于细粒度方案，并发交替机会更多但锁开销较大；把整个循环放进临界区属于
粗粒度方案，开销较小但两个 CPU 实际串行执行。

### 并行输出

临时移除 `printf` 外层锁后，让两个 CPU 同时打印长串 `A` 和 `B`，出现了如下字符级交错：

```text
CCPU1-BBBBBBPU0-AAAAAAAAAABBBBBBBBBBBBBBBBBBBBBBBBBAAAAABBBBB...
```

恢复锁后，同时打印的每一行都保持完整；不同 CPU 的整行顺序可以变化，这正是所需语义。
上述测试代码均已从最终源码移除。

## 验证记录

| 检查 | 结果 |
| --- | --- |
| `make clean && make build` | 通过，`-Wall -Werror` 无错误 |
| ELF | ELF64 RISC-V，入口 `0x80000000` |
| 双核 QEMU | 按顺序输出 CPU 0、CPU 1 启动信息 |
| 格式化输出 | 所有规定格式及边界用例通过 |
| 自旋锁累加 | `20000/20000` |
| 无锁输出对照 | 成功复现字符交错 |
| GDB | 命中 `main`，实测 `pc=0x8000001c`、`tp=1` |

## 当前边界

Lab1 尚未安装 S-mode trap vector，意外异常不具备完整处理路径；该能力属于后续实验。
本实验的 `-bios none` M-mode 入口仅面向 QEMU。VisionFive 2 将通过 OpenSBI 从 S-mode
进入内核，不能直接复用此入口，相关规划见 [`visionfive2-porting.md`](visionfive2-porting.md)。

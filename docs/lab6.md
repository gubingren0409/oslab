# Lab6 实验报告：抢占式多进程与生命周期

- 学生：谷秉仁
- 实验分支：`lab-6`
- 课程框架：`course/lab6@7effc117`，在本地 Lab5 实现之上增量接入。
- 当前状态：VMware 单核、双核回归、双平台构建、VF2 专项及启动 hart 1～4 实板验收均已通过。

## 实验要求

从唯一用户进程扩展为多进程：进程槽与状态机、独立内核栈、调度器与时钟抢占、
fork/exit/wait、孤儿过继、sleep/wakeup、睡眠锁以及本阶段系统调用。
首进程pid应为1；支持QEMU单核及双核；父子内存独立，用户错误只结束违规进程，
资源不足不得发布不完整子进程。

## 环境

继续使用 VMware Ubuntu 22.04.5 中的既定工具链与 QEMU 5.1/OpenSBI 0.7。
Windows `E:\oslab\oslab` 和 Ubuntu `/home/oslab/lab/oslab` 共用源码。
未使用 Windows 或 WSL 代替实验编译环境。

## 设计与实现

### 状态与资源

32个进程槽分别拥有独立内核栈及guard page。增加EMBRYO表示构造中的不可调度状态，
资源、父子关系与上下文全部就绪后才发布RUNNABLE。pid使用全局锁单调分配。
退出进程只转为ZOMBIE；wait负责用户页、页表、trapframe和mmap节点的回收，
固定内核栈保留供槽复用。

### 调度与多核迁移

每个有效hart扫描RUNNABLE进程。scheduler和进程跨swtch交接同一把进程锁，
防止同一进程在两颗hart同时运行。proc_sched要求仅持进程锁、noff=1及SIE关闭。
恢复执行流时使用当前hart的CPU对象恢复intena，不能写入切换前缓存的CPU对象。
内核trap返回恢复原sepc/sstatus，但不恢复旧tp；用户返回重写实际hart ID并执行fence.i。

### 生命周期与条件等待

fork独立复制用户代码、堆、已增长的栈与mmap物理页和节点；子a0=0，父返回子pid。
wait_lock保护父子关系和检查到睡眠的窗口，锁顺序固定为wait_lock到进程锁。
非法退出码地址使wait失败但保留原ZOMBIE，允许合法地址重试。
退出时把孤儿交给proczero；首进程自身死亡也进入保留ZOMBIE，不panic，其他进程继续调度。

sleep先取得进程锁再释放条件锁；wakeup把匹配通道的等待者转成RUNNABLE。
timer_wait在同一时钟锁下检查和睡眠，每个共享tick唤醒等待者。
睡眠锁利用其内部自旋锁、进程通道与持有者pid构成可睡眠互斥。

### 系统调用与平台兼容

本阶段编号1至10依次为brk、mmap、munmap、print_str、print_int、getpid、
fork、wait、exit、sleep；编号头文件由内核与用户共享，不沿用Lab5的临时测试ABI。
Lab5与Lab6分别保存独立分支。

延续课程Lab5已发布的VF2构建增量。QEMU允许1或2核，单核对象隔离到
target/qemu/hart1，避免改变CPUNUM后误用旧对象。
VF2仍用物理hart1至4、32位UART、硬件A/D和Image头。
共享tick沿用Lab5的boot_hartid策略，避免VF2没有hart0与旧文本要求冲突。
已接入 VF2 v1.3B、4GB，并确认 OpenSBI 1.2、U-Boot、DTB 和内存布局。

## 回归用例

`tests/kernel_memory.c`检查用户复制权限、跨页字符串、独立地址空间、
页表OOM、mmap节点耗尽与完整回滚，并对回收前后的两类空闲页计数。

`tests/user/lab6.c`检查pid1、无子wait、非法syscall、父子堆/栈/mmap隔离、
非法wait地址重试、子进程零页故障、sleep(30)与另一可运行进程并行，以及槽复用。
其他用例检查两次fork输出1/2/4次、31个子槽耗尽后复用、首进程退出后子进程继续、
睡眠锁竞争和跨睡眠保持互斥。测试私有调用63只存在于SELFTEST镜像，不进入正式ABI。

在既定 VMware 环境中运行 tools/test-labs.py lab6，以下各项在单核和双核均通过，
共 10 项：基础进程与内存隔离、fork 输出计数、进程槽耗尽与复用、睡眠锁竞争、
首进程退出后其余进程继续运行。SELFTEST 用例另通过页表回滚、mmap 节点并发回收、
逐核 timer 和共享 tick 检查。

默认 QEMU/VF2 镜像构建成功；5 个 VF2 测试镜像已构建并检查 ELF/Image。
默认 VF2 入口为 0x40200000，Image 的 text_offset=0x200000；重启修复版 image_size=69632。
VF2 基础用例已实际输出 LAB6_PASS：启动 hart=1，计算 hart 1～4 各启动一次；
父子地址空间隔离、wait 重试、用户异常隔离和睡眠唤醒通过。
子进程 pid3 的零地址 load page fault 是故意触发的测试，父进程继续运行并通过验收。
MEMORY_SELFTEST_PASS、MMAP_CONCURRENCY_PASS harts=4、逐核 timer 和共享 tick 均通过；
interval=400000，20 个共享 tick 的 time 差为 8007371。
串口输入 A 返回 UART_SELFTEST_PASS irq=32 hart=4。
日志位于 E:\oslab\board-setup\logs\lab6-direct.log 和 lab6-direct.json。
实板专项结果如下；每次运行均通过内存、并发节点池、四核 timer 与 UART 检查。

| VF2 场景 | 实际结果 |
| --- | --- |
| 基础用例直接 booti，启动 hart1 | LAB6_PASS |
| 基础用例经测试加载器分别选择 hart1、2、3、4 | 4 种均为 LAB6_PASS |
| 两次 fork 的输出计数 | 1、2、4 次，LAB6_FORK_PASS |
| 31 个子槽耗尽后回收、复用 | LAB6_SLOTS_PASS |
| 四核睡眠锁竞争与互斥 | LAB6_LOCKS_PASS |
| 首进程退出、其他进程继续运行 | LAB6_INITEXIT_PASS |

共 9 次 VF2 运行，分场景 JSON 和串口日志位于 board-setup/logs。
结构化结果及镜像校验值见 docs/validation/vf2-results.json。
另在 VMware 的默认双核 QEMU 镜像验证演示程序：子进程 sleep(30) 后退出，
父进程打印 parent: child exit status=1234。该运行未启用 SELFTEST。

## 复现命令

在 VMware Ubuntu 中执行：

```bash
cd /home/oslab/lab/oslab
make clean
make -j2 build
python3 tools/test-labs.py lab6
make -j2 PLATFORM=vf2
python3 tools/check-vf2-image.py target/vf2/kernel/kernel-vf2
make -j2 PLATFORM=qemu
make debug
```

默认用户程序fork一个子进程，子sleep(30)后exit(1234)，父wait并打印退出码。
ELF入口、Image头、单/双核日志、UART及VF2上述实板场景均已核实。

QEMU 原始日志位于 E:\oslab\board-setup\logs\lab6-qemu-*.log。
开发板镜像已通过串口安装到 TF 卡的 /boot/oslab，原 Ubuntu 引导配置保留。

## 软件重启补充

重启前恢复 PMIC 使用的 I²C5，正常运行和 panic 循环均接受串口 Ctrl+R。
Lab6 正式镜像和 SELFTEST 镜像已在实板软件重启返回 U-Boot，原 VMware 环境的
10 项 QEMU 回归通过。SDIO BootROM 偶发读卡失败仍需处理；SPI 同版本固件更新
已完成只读备份与预检，尚未写入，连续 13 项自动切换与 panic 完整恢复待验收。
机制、命令和当前证据见 [软件重启说明](vf2-software-reboot.md)及
[结构化记录](validation/vf2-reboot-results.json)。

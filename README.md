# Lab6 实验报告：抢占式多进程与生命周期

- 学生：谷秉仁
- 实验分支：`lab-6`
- 课程框架：`course/lab6@7effc117`，在本地 Lab5 实现之上增量接入。
- 当前状态：VMware 单核、双核 10 项回归和双平台构建已通过，VF2 实板运行验收正在进行。

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
默认 VF2 入口为 0x40200000，Image 的 text_offset=0x200000，image_size=65536。
实板结果须以实际串口日志补录，不用 QEMU 结果代替。

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
完整验收还须检查ELF入口、Image头、单/双核日志、UART，以及VF2实板。

QEMU 原始日志位于 E:\oslab\board-setup\logs\lab6-qemu-*.log。
开发板镜像已通过串口安装到 TF 卡的 /boot/oslab，原 Ubuntu 引导配置保留。

# Lab9 实验报告：文件管理与全系统整合

学生：谷秉仁。实验分支：`lab-9`，基于已验收的 `lab-8@acab89f`。

## 要求与框架来源

接入课程 `lab9@8dc3abe1738c36574cb586e1437595d6ed21ab5c`，完成文件对象和描述符、相对路径与工作目录、硬链接、设备文件、行缓冲控制台、ELF 执行及全系统用户态测试。保留原 VMware 环境及前序内核成果。原始要求见 [课程文档](course/lab9.md)，接口编号见 [syscall_num.h](../src/user/syscall_num.h)。

## 实现

### 文件与进程资源

每进程最多 10 个描述符，全局 128 个 file 对象；file 保存 inode、访问权限、偏移和引用数。独立 open 获得独立偏移，dup 和 fork 共享同一个 file 及偏移，并用睡眠锁串行化读写、seek 和状态查询。最后一个 file 引用关闭后释放 inode 引用。

进程保存 cwd，fork 复制 cwd 和描述符引用；exit 在取得 wait/process 自旋锁之前关闭文件并释放 cwd，避免磁盘 I/O 睡眠时持有生命周期锁。proc_free 检查资源已关闭，再按 Lab6 原流程回收地址空间。成功 exec 保留 cwd/描述符，替换地址空间并清理旧 mmap 元数据。

路径支持绝对/相对形式、多重斜杠、`.`、`..`。目录项变更由命名空间睡眠锁保护，失败创建会回收 inode。硬链接更新 nlink；禁止目录硬链接、根目录及点目录项删除，非空目录不能删除。已 unlink 但仍打开的普通文件可以继续读写，最后一个引用释放后回收。

### 设备与输入输出

启动时建立 `/dev` 和主设备号 2～7 的 stdin、stdout、stderr、zero、null、gpt0。stdin 使用 128 字节行缓冲，支持退格、Ctrl+U 清行和回车转换；stderr 带 ERROR 前缀。设备访问检查打开权限、用户地址和长度，gpt0 的输入限制在本地缓冲容量内。

按照课程接口约定，read/write 失败返回 0；open、close、dup、路径与状态接口失败返回 -1。普通文件 seek 尽力限制在 `[0,size]`。目录枚举只复制有效目录项，缓冲不足时按完整目录项截取。

### ELF 与内存

用户程序链接为 ELF64、小端、RISC-V ET_EXEC，文本段 RX，数据/BSS 段 RW。装载器检查 ELF/程序头范围、段大小、地址边界、对齐、入口执行权限和重复映射；按段标志调用带权限参数的 uvm_heap_grow，BSS 由清零页面初始化。

exec 先构造独立页表、trapframe、用户栈与 argv，全部成功后才替换进程。参数最多 32 个、每项含 NUL 最多 128 字节，总布局须能装入一页参数栈；栈保持 16 字节对齐。失败释放新资源并保留原程序。新的 heap_base 防止 brk 缩小到 ELF 段内，返回用户态时执行 fence.i，覆盖迁移 hart 后的指令可见性。

补齐 pmem_stat，按固定锁顺序读取两个物理页池的可用量。分配状态位图替代释放时的全链表查重，保留范围、对齐、重复释放、计数和清零语义，使整池回收不再反复遍历空闲链表。

### 镜像与程序入口

mkfs 保留 Lab8 的 4096 字节块及 inode 磁盘格式，支持输入 ELF 文件和间接索引，打包课程 test_1～test_4 及验收程序。已有镜像不覆盖；新测试使用新镜像名。默认 initcode 依次 fork/exec/wait 运行用户程序并检查退出状态。

课程 test_3 的读回字符串缓冲原本没有保证 NUL，已初始化缓冲并保留终止符位置；测试的文件操作顺序和内容保持一致。

## 环境与复现

继续使用原 VMware Ubuntu 22.04.5，GCC 11.4.0、binutils 2.38、QEMU 5.1.0 / OpenSBI 0.7。Windows `E:\oslab\oslab` 与 Ubuntu `/home/oslab/lab/oslab` 为同一共享源码目录。内核、用户程序与 mkfs 均在该 Ubuntu 构建，未改用 WSL 或 Windows 编译环境。

```sh
cd /home/oslab/lab/oslab
python3 tools/test-fs.py lab9
python3 tools/test-labs.py lab6
python3 tools/test-fs.py lab8-full
make -j2 PLATFORM=vf2 build
python3 tools/check-vf2-image.py "$(make -s PLATFORM=vf2 print-target)/kernel/kernel-vf2"
```

Lab9 自动测试使用 Ubuntu ext4 上的新稀疏镜像，默认目录为 `/home/oslab/.cache/ecnu-oslab/lab789`。单核/双核镜像分别重启到另一核数后读取 persist.txt，并运行独立的 [镜像检查器](../tools/check-fs-image.py) 核对目录可达性、链接数、块所有权、位图和全部打包 ELF 原始字节。

交互展示执行 `make run`，在 INPUT 提示后输入一行文本，再回答 gpt0 的四个问题（Hello、Guess who I am、How many free memory left、Good job）。最后输出 LAB9_PASS。权限验收会故意让两个子进程访问代码页和已消失的旧 mmap，产生两条用户页错误；父进程检查退出状态后继续输出 LAB9_ELF_PASS。

## 验证结果

- 课程四个用户测试在单核、双核均通过；QEMU UART 输入覆盖 Ctrl+U、退格、回车和行缓冲，核对回显内容。
- 无效描述符/用户指针、设备权限、长名称、目录删除约束、dup/fork 共享偏移、打开后 unlink、inode 复用、描述符耗尽及 24 轮子进程退出回收。
- 损坏/截断 ELF、错误 machine、filesz 大于 memsz、非法 VA、非法页权限、重叠段、参数越界和内存耗尽；失败后原程序继续执行。
- ELF 参数、BSS、RW 数据、代码页写保护、旧 mmap 消失、动态堆下界及用户错误隔离。
- 双进程共享偏移的 128 条完整写入记录、并发目录操作、四进程独立磁盘写入及退出清理。
- 单核、双核、默认 16384 缓存槽配置，以及交叉核数重启后的持久化读取。
- 独立镜像检查确认最终 20 个可达 inode、112 个数据/索引块，无位图泄漏；8 个 ELF 与构建产物逐字节一致，字母表文件、ABC.txt 和 persist.txt 内容一致。
- Lab6 十项单/双核回归、文件系统启用时的旧 fork 用例、Lab8 受限磁盘回归，以及 VF2 构建和 Image 头检查。完整结果见 [验证记录](verification/lab9.txt)。

## 并发中断修正

四进程并发写盘发现了 VirtIO 完成中断确认的竞态：完成队列已增加，但尾部确认清掉了新通知。现已改为先确认中断再处理完成队列，并回填到 Lab7、Lab8。两个前序分支分别通过单核及三轮双核四进程块写入、缓存回收读回和镜像核对，Lab9 另有三轮双核文件系统压力回归。见 [并发中断记录](verification/virtio-irq.txt)。

## 阶段兼容与开发板状态

Lab9 按课程要求将公开调用重新编号为 1～22；旧用户 ELF 需要按本阶段头文件重新编译。旧测试源码通过 LEGACY_TEST 获得专用 print 输出调用，make 选择 lab6/lab6_* 或 lab8 测试时自动启用并隔离产物；正常 Lab9 保持课程 ABI。Lab0～Lab8 的原独立分支保留。

VF2 延续 0x40200000 入口、64 字节 Image 头、原串口/时钟/调度接口，并保留 SDIO 驱动。首次实板存储测试仍须核实并配置独立数据分区的 SDIO_FS_FIRST_SECTOR 和 SDIO_FS_SECTORS；缺省禁止挂载，分区参数也隔离构建目录。本次未接板及写入 TF/SPI，实板文件系统尚未验收。当前实现面向课程静态 ELF，尚无日志、断电恢复和 Linux 用户程序 ABI。

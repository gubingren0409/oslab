# Lab0 实验报告：环境搭建与 xv6 验证

- 学生：谷秉仁
- 实验分支：`master`
- 学期：华东师范大学 2026 秋季学期
- 完成状态：环境配置、xv6 启动与 GDB 调试均已验证

## 1. 实验目标

1. 在 VMware 中建立与课程一致的 Ubuntu 22.04 开发环境。
2. 配置 RISC-V 交叉编译器、binutils、QEMU、GDB、Git 和 Make。
3. 编译并运行 xv6-riscv-2020 `util` 分支，进入 xv6 shell。
4. 用 GDB 连接 QEMU 并命中内核 `main`。
5. 验证 Windows 宿主机与 Ubuntu 虚拟机之间的代码双向同步。

## 2. 实验环境

| 项目 | 实际配置 |
| --- | --- |
| 虚拟化 | VMware Workstation Pro 17 |
| 操作系统 | Ubuntu 22.04.5 LTS |
| 虚拟机资源 | 4 vCPU、4 GiB 内存、64 GiB 虚拟磁盘、NAT |
| RISC-V GCC | 11.4.0 |
| binutils | 2.38 |
| QEMU | 5.1.0，按课程要求使用源码编译版本 |
| GDB multiarch | 12.1 |
| Make | 4.3 |
| xv6 | `xv6-labs-2020`，`util@40db4f0` |

## 3. 开发目录与同步方式

后续内核项目使用 VMware Shared Folders 共享同一份源码：

```text
Windows: E:\oslab\oslab
Ubuntu:  /home/oslab/lab/oslab
实际挂载: /mnt/hgfs/oslab-host/oslab
```

Windows 侧用于编辑代码，交叉编译、QEMU 和 GDB 始终在 Ubuntu 中执行。从任意一侧
修改共享目录后，另一侧能立即看到变化，因此无需手工拷贝源码。

xv6 验收仓库位于：

```text
/home/oslab/lab/xv6-labs-2020
```

## 4. 验证过程与实际结果

### 4.1 编译与启动 xv6

```bash
cd /home/oslab/lab/xv6-labs-2020
git switch util
make clean
make -j4
make qemu
```

编译成功后进入 xv6 shell，执行：

```text
$ echo LAB0_OK
LAB0_OK
```

上述输出说明 RISC-V 交叉编译、xv6 内核、QEMU CPU 模拟与用户态命令执行路径
均已正常工作。退出 QEMU 时先按 `Ctrl+A`，松开后按 `X`。

### 4.2 GDB 远程调试

终端 A：

```bash
cd /home/oslab/lab/xv6-labs-2020
make qemu-gdb
```

终端 B：

```bash
cd /home/oslab/lab/xv6-labs-2020
gdb-multiarch -q -x .gdbinit
(gdb) break main
(gdb) continue
```

GDB 能成功连接 QEMU，并在 `kernel/main.c` 的 `main` 命中断点。这说明 ELF 符号、
QEMU GDB stub 和多架构 GDB 能够配合工作。

### 4.3 完成情况

| 检查项 | 结果 |
| --- | --- |
| VMware 与 Ubuntu 22.04.5 | 通过 |
| RISC-V GCC/binutils、GDB、Git、Make | 通过 |
| QEMU 5.1.0 源码编译 | 通过 |
| xv6 `util` 分支编译 | 通过 |
| xv6 QEMU 启动与 shell 命令 | 通过 |
| QEMU GDB 远程调试 | 通过 |
| Windows/Ubuntu 共享目录双向同步 | 通过 |
| 个人 Gitee 仓库及 Git 远端 | 已建立 |

## 5. 理解与后续实验的关系

Lab0 的核心不是修改 xv6 内核，而是建立一条可重复的“编辑—交叉编译—模拟运行—调试”
工作流。VMware 共享文件夹解决了宿主机编辑和虚拟机构建之间的同步问题，但它只是
文件映射；真正的 Linux/RISC-V 构建和运行仍由 VMware 中的 Ubuntu 完成。

这一环境后续直接用于 Lab1 的裸机启动、UART 与自旋锁，以及 Lab2 的 OpenSBI、物理内存
和 Sv39 页表实验。

## 6. 结论

Lab0 所需的开发环境、xv6 编译运行、GDB 调试和主客机代码共享均已完成验证，
可以作为后续所有 Lab 的稳定基础。补充记录见 [`docs/lab0.md`](docs/lab0.md)。

## 7. Git 约定

- `master` 保存 Lab0 稳定检查点，后续每个 Lab 从前一实验分支继承。
- 个人成果只推送到 `origin`，课程发布远端 `course` 保持只读。
- 不提交生成文件、虚拟机镜像或本机密钥。

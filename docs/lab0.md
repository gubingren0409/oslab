# Lab0：实验环境配置

## 实验要求

Lab0 的目标是准备后续实验环境，并以成功编译和运行 xv6-riscv-2020 作为验收标准。课程推荐的软件栈为：

- VMware Workstation Pro 17
- Ubuntu 22.04
- QEMU 5.1.0
- RISC-V GCC、binutils 与 GDB
- Git、Make 与 VS Code
- xv6-riscv-2020

同时需要熟悉 Git、Markdown、Makefile、Linux 命令行和多文件 C 项目的基础使用方式。

## 完成情况

| 项目 | 结果 |
| --- | --- |
| VMware Workstation Pro 17 | 已完成 |
| Ubuntu 22.04.5 LTS | 已完成 |
| 4 vCPU、4 GB 内存、64 GB 虚拟磁盘、NAT | 已完成 |
| RISC-V GCC/binutils、GDB、Git、Make | 已完成 |
| QEMU 5.1.0 源码编译 | 已完成 |
| xv6 `util` 分支编译 | 已通过 |
| xv6 QEMU 启动与 shell 命令 | 已通过 |
| QEMU GDB 远程调试 | 已通过 |
| Windows 与 Ubuntu 代码实时共享 | 已通过 |
| 个人 Gitee 仓库 | 已建立 |

## 验证记录

环境验证时完成了以下操作：

1. 在 Ubuntu 22.04 中编译 xv6。
2. 使用 QEMU 5.1.0 启动 xv6。
3. 进入 xv6 shell 并成功执行 `echo LAB0_OK`。
4. 使用 `gdb-multiarch` 连接 QEMU，在 `kernel/main.c` 的 `main` 函数命中断点。
5. 分别从 Windows 和 Ubuntu 写入共享测试文件，确认双向修改立即可见。

## 常用命令

```bash
cd ~/lab/xv6-labs-2020
make -j4
make qemu
```

退出 QEMU：先按 `Ctrl+A`，松开后按 `X`。

## 结论

Lab0 的环境配置、运行验证以及个人 Git 仓库准备均已完成，可以进入 Lab1。

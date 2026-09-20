# OSLab

华东师范大学 2026 秋季学期操作系统实验项目。

- 成员：谷秉仁
- 目标：基于 C 与 RISC-V，从零逐步实现一个小型操作系统内核
- 参考系统：[xv6-riscv](https://github.com/mit-pdos/xv6-riscv)
- 当前进度：Lab0 环境配置完成

## 当前环境

- VMware Workstation Pro 17
- Ubuntu 22.04.5 LTS
- QEMU 5.1.0（源码编译）
- RISC-V GCC 11.4.0、binutils、GDB Multiarch
- Git、Make、VS Code

Lab0 已完成 xv6 的编译、QEMU 启动和 GDB 连接验证。详细记录见 [`docs/lab0.md`](docs/lab0.md)。

## 开发入口

Windows 上的项目目录会通过 VMware Shared Folders 映射到 Ubuntu：

```text
Windows: E:\oslab\oslab
Ubuntu:  /home/oslab/lab/oslab
```

进入虚拟机后可以执行：

```bash
cd ~/lab/oslab
git status
```

后续实验的内核源码、实验文档和测试代码将持续提交到本仓库。

## Git 约定

- `main` 始终保留可构建、可运行的版本。
- 每完成一个独立功能或修复进行一次提交。
- 提交信息简要说明改动目的，不提交生成文件、虚拟机镜像或本机密钥。

# 课程发布仓库状态

检查时间：2026-09-20（Asia/Shanghai）

## 连接方式

- 发布仓库：<https://gitee.com/christinaaa/ecnu-oslab-2026-task>
- 个人仓库远端：`origin`
- 课程发布远端：`course`
- 本地发布仓库镜像：`E:\oslab\ecnu-oslab-2026-task`

个人仓库与课程仓库拥有不同的提交历史，因此不直接合并教师分支。教师分支用作只读模板，
每次实验开始时将当次框架导入自己的 `lab-N` 分支。

检查更新：

```powershell
./tools/check-course-release.ps1
```

也可以手动执行：

```powershell
git fetch course --prune
git branch -r
git log -1 --oneline course/master
```

## 当前发布情况

| 内容 | 状态 | 远端位置 | 检查时提交 |
| --- | --- | --- | --- |
| Lab0 | 已发布 | `course/master` 的 Lab0 PPT 和环境文档 | `bd82890` |
| Lab1 | 已正式发布 | `course/master` 的 Lab1 PPT、`course/lab1` 的代码框架 | `bd82890` / `e1f2efd` |
| Lab2 | 框架已公开，尚不能据此认定正式布置 | `course/lab2`；`master` 尚无 Lab2 PPT | `79b9ad3` |
| Lab3 及以后 | 未发现 | 无对应分支或课件 | — |

远端目前只有 `master`、`lab1`、`lab2` 三个分支，没有 Git 标签或 Gitee Release。

## Lab1 核心要求

1. 在 QEMU `virt` 上完成双核启动，执行路径为 `entry.S -> start.c -> main.c`。
2. 从 M-mode 切换到 S-mode，并为各 hart 准备独立栈和 CPU 编号。
3. 基于 UART 实现 `printf`，支持 `%d`、`%p`、`%x`、`%c`、`%s`。
4. 实现自旋锁，用它保护 `printf` 对 UART 的完整占用，避免多核输出交错。
5. 完成并行加法与并行输出实验；测试代码和结果写入 README，不保留在最终内核代码中。
6. 每次实验从上一实验分支创建新分支；学生分支采用 `lab-1`、`lab-2` 等命名，并及时提交。

Lab1 的非强制进阶目标包括 UEFI、从 DTB 动态读取 CPU/内存/设备信息，以及用
`BootInfo` 统一向内核传递启动信息。这三项与未来移植到物理开发板直接相关。

## 当前个人分支状态

- `master`：按课程命名保存 Lab0 稳定检查点。
- `main`：建仓时产生的默认分支，目前与 `master` 指向同一提交；在 Gitee 默认分支切换前保留。
- `lab-1`：已从 Lab0 基线创建，并导入 `course/lab1` 框架。
- 尚未把 `course/lab2` 导入个人仓库；在助教明确发布 Lab2 前只用于了解后续接口方向。

Lab1 框架现已完成实现，并在 VMware Ubuntu 中通过 `make clean && make build`、双核 QEMU、
并发锁和 GDB 验证。实现与测试记录见 [`lab1.md`](lab1.md)。

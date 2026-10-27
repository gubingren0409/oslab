# 课程发布仓库状态

检查日期：2026-10-27（America/Los_Angeles）。

## 仓库连接

- 课程只读远端：`course`，https://gitee.com/christinaaa/ecnu-oslab-2026-task.git
- 个人远端：`origin`，https://gitee.com/gubingren0409/oslab.git
- 课程资料镜像：`E:\oslab\ecnu-oslab-2026-task`
- 最新master：`8bea92be52e2685855b29f1a319948561e147798`。

课程框架按实验增量接入，保留自己的前序实现；不直接覆盖历史实验代码。

## 发布和实现状态

| 实验 | 发布依据 | 个人状态 |
| --- | --- | --- |
| Lab0～Lab4 | 原有任务分支和课件 | 已完成并通过前次VMware验收，历史分支保留 |
| Lab5 | lab5@4a0f4b72；master新增Lab5 PDF | Spec、代码与测试已实现；VMware与实板验收待完成 |
| Lab6 | lab6@7effc117的README、框架、Spec与验收场景 | Spec、代码与测试已实现；VMware验收待完成 |
| Lab7及以后 | 当前未发现对应发布分支 | 尚未开始 |

最新远端分支为master和lab1至lab6，仍未发现Git Tag。Lab6虽没有单独课件，
任务分支内已有明确的要求和验收条件，按已发布实验处理。

Lab5最新增量把VisionFive 2支持列入要求，已经接入PLATFORM=qemu/vf2、
Image头、物理hart ID、UART/PLIC与平台PTE设置，真实硬件结果另行验收。
Lab6从已实现的Lab5分支派生，更新系统调用编号并接入多进程。

## 环境与当前限制

实验编译、QEMU和GDB仍固定在VMware Ubuntu 22.04.5。
本次VMAuthdService停止，UAC服务恢复被取消，SSH不可用，尚未执行新代码构建。
测试脚本和报告明确区分代码实现与实际通过，不使用宿主机或WSL编译替代。

检查新发布：

```powershell
./tools/check-course-release.ps1
```

实验报告分别见根README、docs/lab5.md、docs/lab6.md。

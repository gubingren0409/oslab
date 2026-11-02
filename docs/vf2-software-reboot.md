# VisionFive 2 软件重启

## 内核修改

正常运行和 panic 状态均支持串口 `Ctrl+R` 请求冷重启。实现位于
`src/kernel/lib/reset_vf2.c`，仅对 `PLATFORM=vf2` 生效。

U-Boot 交接内核时会撤销 DesignWare I²C 控制器；本板 I²C5 的时钟仍打开，
但复位位已置位，控制器主机模式及速率寄存器未配置。固件的 PMIC 重启函数
没有完整恢复控制器，因此原先出现 `pmic_ops: cannot read pmic power register`。

重启前先用原生 32 位 MMIO 恢复 I²C5 的时钟、复位、主机模式和保守速率，
读取 AXP15060 的电源寄存器，再调用 SBI SRST 冷重启。两页设备寄存器只映射到内核。
该路径有超时和错误返回，不获取内核锁；panic 循环直接轮询 UART，允许在中断关闭时
处理重启请求。正式镜像包含重启功能，SELFTEST 另提供 `P` 诊断和 `!` panic 验证入口。

依据：[厂商 U-Boot I²C 驱动](https://github.com/starfive-tech/u-boot/blob/JH7110_VisionFive2_devel/drivers/i2c/designware_i2c.c)、
[厂商 OpenSBI PMIC 重启实现](https://github.com/starfive-tech/opensbi/blob/JH7110_VisionFive2_devel/platform/generic/starfive/jh7110.c)。

## 当前验证结果

- 在原 VMware Ubuntu 22.04.5 环境构建，沿用 GCC 11.4、binutils 2.38、QEMU 5.1 和 OpenSBI 0.7。
- Lab5 双核 QEMU SELFTEST、Lab6 单核及双核共 10 项 QEMU 回归通过。
- Lab5/Lab6 正式及 SELFTEST VF2 镜像已构建并检查 Image，9 个部署文件均通过 TF 卡端 SHA-256 校验，旧文件另存备份。
- Lab6 SELFTEST 实板读取 PMIC 电源寄存器得到 0x24，软件冷重启回到 U-Boot。
- Lab6 正式镜像运行后，`Ctrl+R` 软件冷重启回到 U-Boot。
- panic 后 `Ctrl+R` 已进入 BootROM，但随后遇到 SDIO `BOOT fail`；该项尚不能算作完整恢复通过。

本次修复后的 13 项连续实板自动切换尚未验收。此前已通过的实验功能记录保留在
`docs/validation/vf2-results.json`，不能替代本次软重启验收。

## SPI 启动准备状态

SDIO BootROM 存在偶发读卡超时。厂商[启动模式说明](https://doc-en.rvspace.org/VisionFive2/Quick_Start_Guide/VisionFive2_SDK_QSG/boot_mode_settings.html)
建议采用 1-bit QSPI NOR 模式，拨码值为 RGPIO1=0、RGPIO0=0。
本板原 SPI 为 2023-02-28 构建的旧固件，直接切换没有启动输出。

已完整备份原 SPI 的 16MiB 内容并在 TF 卡和 Windows 各留一份，解压与 SHA-256 校验通过。
候选固件从当前官方 Ubuntu 24.04.3 VF2 6.0 TF 镜像提取，与板上正在使用的数据逐字节哈希一致；
U-Boot 构建日期为 2025-09-15。更新只涉及 mtd0 和 mtd2，保留 mtd1 环境区及 TF 原始分区。

`tools/vf2-spi-update.py` 默认执行只读 `prepare`，已在板上通过预检。
`program` 和 `restore` 是单独操作，必须核对完整 SPI 哈希。电脑端计划为
`E:\oslab\board-setup\firmware\SPI-update-plan.md`。
**目前尚未写入 SPI，等待固件更新确认；实际拨码仍为 TF 启动 0/1。**

## 串口自动操作

在 VMware 中启动现有 minicom 会话：

```bash
cd /home/oslab/lab/oslab
python3 tools/start-vf2-console.py
tmux attach -t vf2-serial
```

捕获文件写入 VMware 的 ext4 磁盘，再分批镜像到 Windows 日志，减少 HGFS 上逐字节写入。
仍需校验串口传输文件，不能只根据终端显示判断传输完整。
保持 USB 串口直通 VMware，115200、8N1，关闭软硬件流控；不同时打开其他串口读取程序。

重启脚本会识别官方 Ubuntu 提示符并执行 Linux 重启；实验内核则发送 `Ctrl+R`。
拦截 U-Boot 后核对加载大小、CRC32、DTB，再执行 booti。
**SPI 启动验证完成后**，使用以下命令连续验证 13 个场景：

```bash
python3 tools/vf2_console.py reboot-uboot
python3 tools/test-vf2.py suite
```

每项同时要求实验测试通过和软件重启返回 U-Boot，最终自动启动并登录原 Ubuntu，
重新核对部署文件 SHA-256。`suite --resume` 只接受镜像哈希匹配且已验证软件重启的记录。

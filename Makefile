# 引入通用配置文件
include common.mk

# 裸 make 默认构建完整内核，避免依赖文件中的首个目标接管默认目标。
.DEFAULT_GOAL := build

# 配置CPU核心数量
CPUNUM = 2
# 定义目标文件输出目录
TARGET = target
# 定义各模块路径
KernelPath = src/kernel
UserPath = src/user
# 内核链接脚本
KERNEL_LD  = kernel.ld
# 用户链接脚本
USER_LD = user.ld
# 定义内核目标文件路径
ELFKernel = $(TARGET)/kernel/kernel-qemu.elf
NakedKernel = $(TARGET)/kernel/kernel-qemu.bin
# 用户程序以及由它生成的内嵌头文件
ELFUser = $(TARGET)/user/initcode.elf
NakedUser = $(TARGET)/user/initcode.bin
InitcodeHeader = $(UserPath)/initcode.h

# 收集内核源代码文件（.c和.S汇编文件）
KernelSourceFile = $(wildcard $(KernelPath)/*.c) $(wildcard $(KernelPath)/*.S)
KernelSourceFile += $(wildcard $(KernelPath)/*/*.c) $(wildcard $(KernelPath)/*/*.S)
# 收集用户态源代码文件
UserSourceFile = $(wildcard $(UserPath)/*.c)

# 生成目标文件（.o）路径列表
KernelOBJ = $(patsubst $(KernelPath)/%.S, $(TARGET)/kernel/%.o, $(filter %.S, $(KernelSourceFile)))
KernelOBJ += $(patsubst $(KernelPath)/%.c, $(TARGET)/kernel/%.o, $(filter %.c, $(KernelSourceFile)))
UserOBJ = $(patsubst $(UserPath)/%.c, $(TARGET)/user/%.o, $(filter %.c, $(UserSourceFile)))
USER_CFLAGS = $(CFLAGS) -msmall-data-limit=0

# QEMU模拟器配置
QEMU     = qemu-system-riscv64  # 指定QEMU程序
QEMUOPTS = -machine virt -bios default -kernel $(ELFKernel)  # 使用OpenSBI启动S-mode内核
QEMUOPTS += -m 130M -smp $(CPUNUM) -nographic  # 物理内存从0x80000000到0x88200000

# 调试相关配置
GDBPORT = $(shell expr `id -u` % 5000 + 25000)  # 动态计算GDB端口号
# 根据QEMU版本选择合适的GDB调试参数
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# 头文件变化也必须触发对应目标重新编译。
-include $(KernelOBJ:.o=.d) $(UserOBJ:.o=.d)

# 生成GDB初始化文件
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

# 运行目标：先构建再启动QEMU
run: build
	$(QEMU) $(QEMUOPTS)

# 调试目标：启动带GDB调试的QEMU
debug: build .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# 构建目标：创建输出目录并编译内核
build: $(TARGET) $(ELFKernel)

# 创建输出目录结构（如果不存在）
.PHONY: $(TARGET)
$(TARGET):
	@mkdir -p $(TARGET)/kernel
	@mkdir -p $(TARGET)/kernel/arch
	@mkdir -p $(TARGET)/kernel/boot
	@mkdir -p $(TARGET)/kernel/lock
	@mkdir -p $(TARGET)/kernel/lib
	@mkdir -p $(TARGET)/kernel/mem
	@mkdir -p $(TARGET)/kernel/trap
	@mkdir -p $(TARGET)/kernel/proc
	@mkdir -p $(TARGET)/user

# 编译规则：将汇编文件(.S)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.S | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译规则：将C文件(.c)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译用户态程序
$(TARGET)/user/%.o: $(UserPath)/%.c | $(TARGET)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

# 用户程序从 USER_BASE 链接，再转换为内核可以直接复制的一页平坦映像。
$(ELFUser): $(UserOBJ) $(USER_LD) | $(TARGET)
	$(LD) $(LDFLAGS) -T $(USER_LD) $(UserOBJ) -o $@

$(NakedUser): $(ELFUser)
	$(OBJCOPY) -O binary $< $@

$(InitcodeHeader): $(NakedUser)
	cd $(dir $<) && xxd -i $(notdir $<) > $(abspath $@)

# proc.c 直接包含生成的数组，必须在编译它之前准备 initcode.h。
$(TARGET)/kernel/proc/proc.o: $(InitcodeHeader)

# 链接生成内核ELF文件
$(ELFKernel): $(KernelOBJ) $(KERNEL_LD) | $(TARGET)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) $(KernelOBJ) -o $@

# 清理目标：删除输出目录
.PHONY: clean
clean:
	rm -rf target
	rm -f $(InitcodeHeader)

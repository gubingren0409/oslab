.DEFAULT_GOAL := build
PLATFORM ?= qemu
ifneq ($(PLATFORM),qemu)
ifneq ($(PLATFORM),vf2)
$(error unsupported PLATFORM: $(PLATFORM))
endif
endif

# 引入通用配置文件
include common.mk

# 沿用助教规定的 riscv64-linux-gnu 工具链。
CPUNUM ?= 2
ifneq ($(CPUNUM),2)
$(error Lab5 requires CPUNUM=2)
endif
# 两个平台的生成文件完全隔离。
TARGET = target/$(PLATFORM)
TEST ?=
ifneq ($(TEST),)
TARGET := $(TARGET)/$(TEST)
endif
ifeq ($(SELFTEST),1)
TARGET := $(TARGET)/selftest
CFLAGS += -DOSLAB_SELFTEST
endif
CFLAGS += -I$(TARGET)/user -Isrc/user
USER_CFLAGS = $(CFLAGS) -msmall-data-limit=0
ifeq ($(PLATFORM),vf2)
CFLAGS += -DVISIONFIVE2
endif
# 定义各模块路径
KernelPath = src/kernel
UserPath = src/user
# 内核链接脚本
KERNEL_LD  = $(TARGET)/kernel/kernel.ld
# 用户程序链接脚本 (用户程序从USER_BASE = 0x1000开始执行)
USER_LD    = user.ld
# 定义内核目标文件路径
ELFKernel = $(TARGET)/kernel/kernel-$(PLATFORM).elf
NakedKernel = $(TARGET)/kernel/kernel-$(PLATFORM).bin
# 定义用户程序的目标文件路径
ELFUser = $(TARGET)/user/initcode.elf
NakedUser = $(TARGET)/user/initcode.bin
# initcode数组的头文件 (由proc.c直接#include)
InitcodeHeader = $(TARGET)/user/initcode.h

# 收集内核源代码文件（.c和.S汇编文件）
KernelSourceFile = $(wildcard $(KernelPath)/*.c) $(wildcard $(KernelPath)/*.S)
KernelSourceFile += $(wildcard $(KernelPath)/*/*.c) $(wildcard $(KernelPath)/*/*.S)
# 收集用户态源代码文件
UserSourceFile = $(if $(TEST),tests/user/$(TEST).c,$(UserPath)/initcode.c)

# 生成目标文件（.o）路径列表
KernelOBJ = $(patsubst $(KernelPath)/%.S, $(TARGET)/kernel/%.o, $(filter %.S, $(KernelSourceFile)))
KernelOBJ += $(patsubst $(KernelPath)/%.c, $(TARGET)/kernel/%.o, $(filter %.c, $(KernelSourceFile)))
UserOBJ = $(TARGET)/user/initcode.o
ifeq ($(SELFTEST),1)
KernelOBJ += $(TARGET)/kernel/selftest.o
endif

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

# 引入编译器生成的依赖文件(.d)
# CFLAGS中的-MD会为每个.o生成同名的.d文件, 里面记录了该目标文件依赖的所有头文件
# 不引入它们的话, 修改头文件(例如timer/type.h中的INTERVAL)不会触发重新编译
-include $(KernelOBJ:.o=.d) $(UserOBJ:.o=.d)

# 生成GDB初始化文件
.PHONY: FORCE
FORCE:
.gdbinit: .gdbinit.tmpl-riscv FORCE
	sed -e "s/:1234/:$(GDBPORT)/" -e "s|@KERNEL@|$(ELFKernel)|" < $< > $@

# 运行目标：先构建再启动QEMU
.PHONY: build run debug clean
ifeq ($(PLATFORM),qemu)
run: build
	$(QEMU) $(QEMUOPTS)

# 调试目标：启动带GDB调试的QEMU
debug: $(ELFKernel) .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)
else
run debug:
	@echo "PLATFORM=vf2: use U-Boot booti"
	@exit 1
endif

# 构建目标：创建输出目录并编译内核
build: $(TARGET) $(ELFKernel)
ifeq ($(PLATFORM),vf2)
build: $(NakedKernel)
endif

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
	@mkdir -p $(TARGET)/kernel/syscall
	@mkdir -p $(TARGET)/user

# 编译规则：将汇编文件(.S)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.S | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译规则：将C文件(.c)编译为目标文件(.o)
$(TARGET)/kernel/%.o: $(KernelPath)/%.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译用户态程序
$(TARGET)/user/initcode.o: $(UserSourceFile) | $(TARGET)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(TARGET)/kernel/selftest.o: tests/kernel_memory.c | $(TARGET)
	$(CC) $(CFLAGS) -c -o $@ $<

# 链接生成用户程序ELF (main位于USER_BASE, 由user.ld保证)
$(ELFUser): $(UserOBJ) $(USER_LD) | $(TARGET)
	$(LD) $(LDFLAGS) -T $(USER_LD) $(UserOBJ) -o $@

# 转换成平坦二进制映像: 第一个字节就是USER_BASE处的指令
$(NakedUser): $(ELFUser)
	$(OBJCOPY) -O binary $< $@

# 转换成内核可以直接使用的数组
$(InitcodeHeader): $(NakedUser)
	cd $(dir $<) && xxd -i $(notdir $<) > $(abspath $@)

# proc.c会#include initcode.h, 因此它必须等待initcode生成完毕
$(TARGET)/kernel/proc/proc.o: $(InitcodeHeader)

# 不使用CFLAGS中的-MD，避免生成的依赖覆盖源链接脚本。
$(KERNEL_LD): kernel.ld | $(TARGET)
	$(CC) -E -P -x c $(if $(filter vf2,$(PLATFORM)),-DVISIONFIVE2) $< -o $@

$(NakedKernel): $(ELFKernel)
	$(OBJCOPY) -O binary $< $@

# 链接生成内核ELF文件
$(ELFKernel): $(KernelOBJ) $(KERNEL_LD) | $(TARGET)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) $(KernelOBJ) -o $@

# 清理目标：删除输出目录
clean:
	rm -rf target

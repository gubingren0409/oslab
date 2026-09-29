#pragma once

// PLIC相关
void plic_init(void);
void plic_inithart(void);
int plic_claim(void);
void plic_complete(int irq);

// 时钟相关。当前项目使用OpenSBI，初始化与计时逻辑留作实验接口。
void timer_init(void);
void timer_create(void);
void timer_update(void);
uint64 timer_get_ticks(void);

// 内核态trap初始化与处理
void trap_kernel_init(void);
void trap_kernel_inithart(void);
void trap_kernel_handler(void);

void external_interrupt_handler(void);
void timer_interrupt_handler(void);

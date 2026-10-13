#include "mod.h"

// 中断信息
static const char *interrupt_info[16] = {
    "U-mode software interrupt", // 0
    "S-mode software interrupt", // 1
    "reserved-1",                // 2
    "M-mode software interrupt", // 3
    "U-mode timer interrupt",    // 4
    "S-mode timer interrupt",    // 5
    "reserved-2",                // 6
    "M-mode timer interrupt",    // 7
    "U-mode external interrupt", // 8
    "S-mode external interrupt", // 9
    "reserved-3",                // 10
    "M-mode external interrupt", // 11
    "reserved-4",                // 12
    "reserved-5",                // 13
    "reserved-6",                // 14
    "reserved-7",                // 15
};

// 异常信息
static const char *exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// 实现位于 trap.S
// 它是完整的内核态trap处理流程
extern void kernel_vector();

#define SCAUSE_INTERRUPT (1ul << 63)

static const char *trap_info(const char *const info[], uint64 cause)
{
    if (cause < 16)
        return info[cause];
    return "unknown";
}

// 初始化trap中各个核心共享的东西
void trap_kernel_init()
{
    // PLIC初始化
    plic_init();

    // 系统时钟创建
    timer_create();
}

// 初始化trap中各个核心独有的东西
void trap_kernel_inithart()
{
    // 填写内核态中断处理函数
    w_stvec((uint64)kernel_vector);

    // PLIC核心初始化，并允许S-mode接收外设中断。
    plic_inithart();
    w_sie(r_sie() | SIE_SEIE);

    // 全局中断由main在所有本hart资源就绪后统一打开。
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();       // 记录了发生异常时的PC值
    uint64 sstatus = r_sstatus(); // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();   // 引发trap的原因
    uint64 stval = r_stval();     // 发生trap时保存的附加信息 (不同trap类型不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interrupt enabled");

    uint64 cause = scause & ~SCAUSE_INTERRUPT;

    /* 高位bit标识了是中断还是异常 */
    if (scause & SCAUSE_INTERRUPT)
    {
        // 1-中断处理
        switch (cause) // 中断产生原因分类
        {
        case 5:
            timer_interrupt_handler();
            break;

        case 9:
            external_interrupt_handler();
            break;

        default: // 例外处理
            printf("\nunexpected interrupt: %s\n",
                   trap_info(interrupt_info, cause));
            printf("scause = %x, sepc = %x, stval = %x\n",
                   scause, sepc, stval);
            panic("trap_kernel_handler");
        }
    }
    else
    {
        // Lab3尚未定义可恢复的同步异常，保留完整上下文并停止内核。
        printf("\nunexpected exception: %s\n",
               trap_info(exception_info, cause));
        printf("scause = %x, sepc = %x, stval = %x\n",
               scause, sepc, stval);
        panic("trap_kernel_handler");
    }
}

// 外设中断处理 (基于PLIC，lab-3只需要识别和处理UART中断)
void external_interrupt_handler()
{
    int irq = plic_claim();

    // claim返回0表示当前没有待处理的外设中断。
    if (irq == 0)
        return;

    if (irq == UART_IRQ)
    {
        uart_intr();
        plic_complete(irq);
        return;
    }

    // 即使是不认识的中断，也先complete，避免PLIC一直保留该请求。
    // 外部设备输入不是内核不变量错误，诊断后继续运行。
    plic_complete(irq);
    printf("unexpected external interrupt: irq = %d\n", irq);
}

// 时钟中断处理 (当前内核的时钟源由OpenSBI设置)
void timer_interrupt_handler()
{
    // 设置下一次时钟事件
    // 如果不设置, stimecmp会一直是一个过期的值, CPU处理完这次中断后会立刻再次响应同一个中断

    // 每个CPU都有独立的定时事件，因此每个CPU都必须重新设置。
    timer_rearm();

    // 只由CPU-0更新共享时钟。
    if (mycpuid() == 0)
        timer_update();
}

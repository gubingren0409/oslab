#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "proc/mod.h"
#include "trap/mod.h"

// _entry在kernel.ld中定义，后续hart从该地址启动。
extern void _entry(void);

// OpenSBI会选择启动hart，它不一定是hart 0。
// 0 = 未初始化，1 = 启动hart正在初始化，2 = 全局初始化完成。
static volatile int init_state = 0;

void main(void)
{
    uint64 hartid = r_tp();

    if (__sync_bool_compare_and_swap(&init_state, 0, 1))
    {
        print_init();
        pmem_init();
        kvm_init();
        trap_kernel_init();

        __sync_synchronize();
        init_state = 2;

        for (uint64 id = 0; id < NCPU; id++)
        {
            if (id == hartid)
                continue;

            sbi_ret_t ret = sbi_hart_start(id, (uint64)_entry, 0);
            if (ret.error != SBI_SUCCESS)
            {
                printf("failed to start cpu %d: error %d\n",
                       (int)id, (int)ret.error);
                panic("sbi_hart_start");
            }
        }
    }
    else
    {
        while (init_state != 2)
            ;
        __sync_synchronize();
    }

    // 以下资源是每个hart独立的。
    kvm_inithart();
    printf("cpu %d is booting with OpenSBI!\n", (int)hartid);

    // 先关闭全局中断并安装完整trap入口，再配置本hart的时钟；
    // 所有入口和使能位就绪后才统一打开全局中断。
    intr_off();
    trap_kernel_inithart();
    uart_enable_rx_interrupt();
    timer_init();
    intr_on();

    // proczero 固定由 hart0 运行；共享初始化 hart 则可能是任意 hart。
    if (hartid == 0)
        proc_make_first();

    while (1)
        asm volatile("wfi");
}

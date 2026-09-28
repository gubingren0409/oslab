#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

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
        kvm_inithart();
        printf("cpu %d is booting with OpenSBI!\n", (int)hartid);

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
        kvm_inithart();
        printf("cpu %d is booting with OpenSBI!\n", (int)hartid);
    }

    while (1)
        asm volatile("wfi");
}

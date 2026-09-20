#include "arch/mod.h"
#include "lib/mod.h"

// CPU 0完成公共资源初始化后，将该标志发布给其他CPU
volatile static int started = 0;

void main(void)
{
    int cpuid = mycpuid();

    if (cpuid == 0)
    {
        print_init();
        printf("cpu %d is booting!\n", cpuid);

        // 保证初始化结果先于started对其他CPU可见
        __sync_synchronize();
        started = 1;
    }
    else
    {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
    }

    while (1)
        asm volatile("wfi");
}

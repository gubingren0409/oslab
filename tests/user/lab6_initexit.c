#include "sys.h"

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    long child = syscall(SYS_fork);
    if (child > 0)
        syscall(SYS_exit, 77);
    if (child == 0)
    {
        syscall(SYS_sleep, 3);
        syscall(SYS_print_str, "LAB6_INITEXIT_PASS\n");
        syscall(SYS_exit, 0);
    }
    while (1)
        asm volatile("" ::: "memory");
}

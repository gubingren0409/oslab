#include "sys.h"

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    int values[5];
    syscall(SYS_copyout, values);
    syscall(SYS_copyin, values, 5);
    syscall(SYS_copyinstr, "hello, world");
    syscall(SYS_printf, "Lab5: syscall/copy ready\n");
    while (1)
        asm volatile("" ::: "memory");
}

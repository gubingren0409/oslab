#include "sys.h"

static __attribute__((noinline)) long helloworld(void)
{
    return syscall(SYS_helloworld);
}

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    (void)helloworld();
    (void)helloworld();
    while (1)
        asm volatile("" ::: "memory");
}

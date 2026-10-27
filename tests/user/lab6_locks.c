#include "sys.h"

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    for (int i = 0; i < 2; i++)
    {
        long pid = syscall(SYS_fork);
        if (pid == 0)
        {
            int good = syscall(63) == 0 && syscall(63) == 0;
            syscall(SYS_exit, good ? 0 : 1);
        }
        if (pid < 0)
            syscall(SYS_print_str, "LAB6_LOCKS_FAIL\n");
    }
    int status = 1, good = 1;
    for (int i = 0; i < 2; i++)
        if (syscall(SYS_wait, &status) < 0 || status != 0)
            good = 0;
    syscall(SYS_print_str, good ? "LAB6_LOCKS_PASS\n" : "LAB6_LOCKS_FAIL\n");
    while (1)
        syscall(SYS_sleep, 100);
}

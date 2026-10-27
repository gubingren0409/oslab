#include "sys.h"

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    int count = 0;
    while (1)
    {
        long pid = syscall(SYS_fork);
        if (pid < 0)
            break;
        if (pid == 0)
        {
            syscall(SYS_sleep, 10);
            syscall(SYS_exit, 0);
        }
        count++;
    }
    int collected = 0;
    while (syscall(SYS_wait, 0) >= 0)
        collected++;
    long pid = syscall(SYS_fork);
    if (pid == 0)
        syscall(SYS_exit, 0);
    int good = count == 31 && collected == count && pid > 0 && syscall(SYS_wait, 0) == pid;
    syscall(SYS_print_str, good ? "LAB6_SLOTS_PASS\n" : "LAB6_SLOTS_FAIL\n");
    while (1)
        syscall(SYS_sleep, 100);
}

#include "sys.h"

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    syscall(SYS_print_str, "proczero: hello world!\n");
    long pid = syscall(SYS_fork);
    if (pid == 0)
    {
        syscall(SYS_print_str, "child: sleeping for 30 ticks\n");
        syscall(SYS_sleep, 30);
        syscall(SYS_exit, 1234);
    }
    else if (pid > 0)
    {
        int status = 0;
        syscall(SYS_wait, &status);
        syscall(SYS_print_str, "parent: child exit status=");
        syscall(SYS_print_int, status);
        syscall(SYS_print_str, "\n");
    }
    while (1)
        syscall(SYS_sleep, 100);
}

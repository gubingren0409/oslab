#include "sys.h"

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    long root = syscall(SYS_getpid);
    syscall(SYS_print_str, "level-1!\n");
    syscall(SYS_fork);
    syscall(SYS_print_str, "level-2!\n");
    syscall(SYS_fork);
    syscall(SYS_print_str, "level-3!\n");
    if (syscall(SYS_getpid) != root)
        syscall(SYS_exit, 0);
    while (syscall(SYS_wait, 0) >= 0)
        ;
    syscall(SYS_print_str, "LAB6_FORK_PASS\n");
    while (1)
        syscall(SYS_sleep, 100);
}

#include "sys.h"
int main(void)
{
    syscall(SYS_print_str,"LAB8_USER_READY\n");
    syscall(SYS_exit,0);
    return 0;
}

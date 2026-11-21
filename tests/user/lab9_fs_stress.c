#include "sys.h"
__attribute__((section(".text.entry"))) int main(void)
{
    char *args[]={"fsprobe",0};
    syscall(SYS_exec,"/fsprobe",args);
    syscall(SYS_write,1,13,"LAB9_FS_FAIL\n"); syscall(SYS_exit,1); return 0;
}

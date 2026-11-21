#include "sys.h"
__attribute__((section(".text.entry"))) int main(void)
{
    char *args[]={"persistprobe",0};
    syscall(SYS_exec,"/persistprobe",args);
    syscall(SYS_write,1,15,"LAB9_COLD_FAIL\n"); syscall(SYS_exit,1); return 0;
}

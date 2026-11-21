#include "sys.h"
#define CHECK(x) do { if (!(x)) { syscall(SYS_print_str,"LAB7_IRQ_FAIL line "); syscall(SYS_print_int,__LINE__); syscall(SYS_exit,1); } } while (0)
__attribute__((section(".text.entry"))) int main(void)
{
    unsigned char *data=(unsigned char *)mmap(0,4096); CHECK(data);
    for (int child=0;child<4;child++) {
        long pid=syscall(SYS_fork); CHECK(pid>=0);
        if (!pid) {
            for (int i=0;i<4096;i++) data[i]='A'+child;
            unsigned long b=syscall(SYS_get_block,5000+child); CHECK(b);
            for (int iteration=0;iteration<128;iteration++) {
                *(unsigned *)data=iteration;
                CHECK(!syscall(SYS_write_block,b,data));
            }
            CHECK(!syscall(SYS_put_block,b));
            syscall(SYS_exit,0);
        }
    }
    for (int i=0;i<4;i++) { int status=-1; CHECK(syscall(SYS_wait,&status)>0 && status==0); }
    syscall(SYS_flush_buffer,8);
    for (int child=0;child<4;child++) {
        unsigned long b=syscall(SYS_get_block,5000+child); CHECK(b);
        CHECK(!syscall(SYS_read_block,b,data));
        CHECK(*(unsigned *)data==127);
        for (int i=4;i<4096;i++) CHECK(data[i]=='A'+child);
        CHECK(!syscall(SYS_put_block,b));
    }
    syscall(SYS_print_str,"LAB7_IRQ_PASS\n"); syscall(SYS_exit,0); return 0;
}

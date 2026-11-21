#include "sys.h"
#define CHECK(x) do { if (!(x)) { syscall(SYS_print_str,"LAB7_BITMAP_FAIL\n"); syscall(SYS_exit,1); } } while (0)
int main(void)
{
    unsigned *sb=(unsigned *)mmap(0,4096); CHECK(sb);
    unsigned long b=syscall(SYS_get_block,0); CHECK(b);
    CHECK(!syscall(SYS_read_block,b,sb)); syscall(SYS_put_block,b);
    unsigned bits[]={32767,32768,65536};
    for (int i=0;i<3;i++) CHECK((unsigned)syscall(SYS_alloc_block)==sb[10]+bits[i]);
    CHECK((unsigned)syscall(SYS_alloc_block)==0xffffffffU);
    for (int i=0;i<3;i++) CHECK((unsigned)syscall(SYS_alloc_inode)==bits[i]);
    CHECK((unsigned)syscall(SYS_alloc_inode)==0xffffffffU);
    syscall(SYS_free_block,sb[10]+65536); syscall(SYS_free_inode,65536);
    syscall(SYS_flush_buffer,8);
    CHECK((unsigned)syscall(SYS_alloc_block)==sb[10]+65536);
    CHECK((unsigned)syscall(SYS_alloc_inode)==65536);
    syscall(SYS_print_str,"LAB7_BITMAP_PASS\n"); syscall(SYS_exit,0);
    return 0;
}

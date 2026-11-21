#include "sys.h"
#define CHECK(x) do { if (!(x)) { syscall(SYS_print_str,"LAB7_FAIL line "); syscall(SYS_print_int,__LINE__); syscall(SYS_exit,1); } } while (0)
int main(void)
{
    unsigned blocks[20], inodes[20];
    for (int i=0;i<20;i++) { blocks[i]=syscall(SYS_alloc_block); CHECK(blocks[i]!=0xffffffffU); }
    syscall(SYS_show_bitmap,0);
    for (int i=0;i<20;i+=2) CHECK(syscall(SYS_free_block,blocks[i])==0);
    for (int i=1;i<20;i+=2) CHECK(syscall(SYS_free_block,blocks[i])==0);
    for (int i=0;i<20;i++) { inodes[i]=syscall(SYS_alloc_inode); CHECK(inodes[i]!=0xffffffffU); }
    for (int i=0;i<20;i++) CHECK(syscall(SYS_free_inode,inodes[i])==0);
    CHECK(syscall(SYS_put_block,0x80000000)==-1);
    unsigned long b=syscall(SYS_get_block,5000);
    CHECK(b);
    CHECK(!syscall(SYS_get_block,5000));
    char *data=(char *)mmap(0,8192); CHECK(data);
    char *tmp=data+4096;
    for (int i=0;i<4096;i++) data[i]=(char)(i*17+3);
    CHECK(syscall(SYS_write_block,b,0)==-1);
    CHECK(!syscall(SYS_write_block,b,data));
    CHECK(!syscall(SYS_put_block,b));
    CHECK(syscall(SYS_put_block,b)==-1);
    syscall(SYS_flush_buffer,16384);
    b=syscall(SYS_get_block,5000); CHECK(b);
    CHECK(!syscall(SYS_read_block,b,tmp));
    for (int i=0;i<4096;i++) CHECK(data[i]==tmp[i]);
    CHECK(!syscall(SYS_put_block,b));
    unsigned long held[5]; int order[]={0,3,7,2,4};
    for (int i=0;i<5;i++) { held[i]=syscall(SYS_get_block,5000+order[i]); CHECK(held[i]); }
    syscall(SYS_show_buffer);
    syscall(SYS_put_block,held[2]); syscall(SYS_put_block,held[0]); syscall(SYS_put_block,held[4]);
    syscall(SYS_flush_buffer,3); syscall(SYS_show_buffer);
    syscall(SYS_put_block,held[1]); syscall(SYS_put_block,held[3]);
    int pid=syscall(SYS_fork); CHECK(pid>=0);
    if (!pid) { CHECK(syscall(SYS_get_block,5010)); syscall(SYS_exit,0); }
    int status=-1; CHECK(syscall(SYS_wait,&status)==pid && status==0);
    b=syscall(SYS_get_block,5010); CHECK(b); syscall(SYS_put_block,b);
    syscall(SYS_print_str,"LAB7_PASS\n");
    syscall(SYS_exit,0);
    return 0;
}

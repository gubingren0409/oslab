#include "help.h"
#define CHECK(x) do { if (!(x)) { fprintf(STDERR,"LAB9_ELF_FAIL line %d\n",__LINE__); sys_exit(1); } } while (0)
volatile uint64 initialized=0x123456789abcdef0ULL;
volatile uint64 zeros[1024];
void main(int argc,char **argv)
{
    CHECK(argc==3 && !strncmp(argv[0],"elf",4) && !strncmp(argv[1],"alpha",6) &&
          !strncmp(argv[2],"beta",5) && argv[3]==NULL);
    uint64 sp; asm volatile("mv %0,sp":"=r"(sp)); CHECK(!(sp%16));
    CHECK(initialized==0x123456789abcdef0ULL);
    for(int i=0;i<1024;i++) CHECK(zeros[i]==0);
    initialized++; zeros[1023]=77;
    CHECK(initialized==0x123456789abcdef1ULL && zeros[1023]==77);
    uint64 base=sys_brk(0); CHECK(base>0x2000);
    CHECK(sys_brk(0x2000)==(uint64)-1);
    CHECK(sys_brk(base+PGSIZE)==base+PGSIZE);
    *(volatile char *)base=12; CHECK(sys_brk(base)==base);
    CHECK(sys_munmap(MMAP_BEGIN,PGSIZE)==(uint64)-1);
    int pid=sys_fork(); CHECK(pid>=0);
    if (!pid) { *(volatile char *)(uint64)main=0; sys_exit(2); }
    uint32 status; CHECK(sys_wait(&status)==(uint32)pid && status==(uint32)-1);
    pid=sys_fork(); CHECK(pid>=0);
    if (!pid) { volatile uint64 value=*(volatile uint64 *)MMAP_BEGIN; sys_exit(value==0?2:3); }
    CHECK(sys_wait(&status)==(uint32)pid && status==(uint32)-1);
    fprintf(STDOUT,"LAB9_ELF_PASS\n"); sys_exit(0);
}

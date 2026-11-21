#include "help.h"
#define CHECK(x) do { if (!(x)) { fprintf(STDERR,"LAB9_FS_FAIL line %d\n",__LINE__); sys_exit(1); } } while (0)
void main(int argc,char **argv)
{
    uint32 fd=sys_open("/parallel",OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(fd!=(uint32)-1);
    for(int child=0;child<2;child++) {
        int pid=sys_fork(); CHECK(pid>=0);
        if(!pid) {
            char record[32]; memset(record,'A'+child,sizeof(record));
            for(int i=0;i<64;i++) CHECK(sys_write(fd,sizeof(record),record)==sizeof(record));
            sys_exit(0);
        }
    }
    uint32 status;
    for(int i=0;i<2;i++) CHECK(sys_wait(&status)!=(uint32)-1 && !status);
    file_stat_t stat; CHECK(!sys_fstat(fd,&stat) && stat.size==4096 && stat.offset==4096);
    CHECK(!sys_lseek(fd,0,LSEEK_SET));
    char *bytes=(char *)sys_mmap(0,PGSIZE); CHECK((uint64)bytes!=0);
    CHECK(sys_read(fd,PGSIZE,bytes)==PGSIZE);
    int a=0,b=0;
    for(int i=0;i<128;i++) {
        char value=bytes[i*32]; CHECK(value=='A' || value=='B');
        for(int j=1;j<32;j++) CHECK(bytes[i*32+j]==value);
        if(value=='A') a++; else b++;
    }
    CHECK(a==64 && b==64); CHECK(!sys_close(fd) && !sys_unlink("/parallel"));
    CHECK(!sys_munmap((uint64)bytes,PGSIZE));
    CHECK(!sys_mkdir("/concurrent"));
    for(int child=0;child<2;child++) {
        int pid=sys_fork(); CHECK(pid>=0);
        if(!pid) {
            CHECK(!sys_chdir("/concurrent"));
            char *name=child ? "B" : "A";
            for(int i=0;i<24;i++) {
                uint32 f=sys_open(name,OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(f!=(uint32)-1);
                CHECK(sys_write(f,3,"xyz")==3 && !sys_close(f) && !sys_unlink(name));
            }
            sys_exit(0);
        }
    }
    for(int i=0;i<2;i++) CHECK(sys_wait(&status)!=(uint32)-1 && !status);
    CHECK(!sys_unlink("/concurrent"));
    uint32 parallel[4]; char *names[]={"/io-A","/io-B","/io-C","/io-D"};
    for(int i=0;i<4;i++) { parallel[i]=sys_open(names[i],OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(parallel[i]!=(uint32)-1); }
    for(int child=0;child<4;child++) {
        int pid=sys_fork(); CHECK(pid>=0);
        if(!pid) {
            char record[256]; memset(record,'a'+child,sizeof(record));
            for(int i=0;i<32;i++) CHECK(sys_write(parallel[child],sizeof(record),record)==sizeof(record));
            sys_exit(0);
        }
    }
    for(int i=0;i<4;i++) CHECK(sys_wait(&status)!=(uint32)-1 && !status);
    for(int i=0;i<4;i++) {
        CHECK(!sys_fstat(parallel[i],&stat) && stat.size==8192);
        CHECK(!sys_close(parallel[i]) && !sys_unlink(names[i]));
    }
    fd=sys_open("/persist.txt",OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(fd!=(uint32)-1);
    CHECK(sys_write(fd,16,"LAB9_PERSIST_OK\n")==16 && !sys_close(fd));
    fprintf(STDOUT,"LAB9_FS_PASS\n"); sys_exit(0);
}

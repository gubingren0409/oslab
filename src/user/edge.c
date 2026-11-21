#include "help.h"
#define CHECK(x) do { if (!(x)) { fprintf(STDERR,"LAB9_EDGE_FAIL line %d\n",__LINE__); sys_exit(1); } } while (0)
static void bad_image(uint32 fd,char *bytes,uint32 len)
{
    char *args[]={"bad",NULL};
    CHECK(sys_lseek(fd,0,LSEEK_SET)==0 && sys_write(fd,len,bytes)==len);
    CHECK(sys_exec("/badelf",args)==(uint32)-1);
}
void main(int argc,char **argv)
{
    CHECK(sys_close(99)==(uint32)-1 && sys_dup(99)==(uint32)-1);
    CHECK(sys_read(99,1,(void *)1)==0 && sys_write(STDOUT,1,(void *)1)==0);
    CHECK(sys_read(STDIN,1,(void *)0)==0);
    CHECK(sys_open("/dev/stdin",OPEN_WRITE)==(uint32)-1);
    CHECK(sys_open("/dev/stdout",OPEN_READ)==(uint32)-1);
    CHECK(sys_mkdir("/")== (uint32)-1 && sys_unlink("/")== (uint32)-1);
    char longname[80]; memset(longname,'x',79); longname[79]=0;
    CHECK(sys_open(longname,OPEN_CREATE|OPEN_WRITE)==(uint32)-1);
    char *data=(char *)sys_mmap(0,4*PGSIZE); CHECK((uint64)data!=0);
    for(int i=0;i<9000;i++) data[i]=(char)(i*7);
    uint32 fd=sys_open("/edge.txt",OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(fd!=(uint32)-1);
    CHECK(sys_write(fd,9000,data)==9000);
    uint32 alias=sys_dup(fd); CHECK(alias!=(uint32)-1);
    CHECK(sys_lseek(alias,0,LSEEK_SET)==0);
    int pid=sys_fork(); CHECK(pid>=0);
    if (!pid) { CHECK(sys_read(fd,32,data+12000)==32); sys_exit(0); }
    uint32 status; CHECK(sys_wait(&status)==(uint32)pid && !status);
    file_stat_t stat; CHECK(!sys_fstat(alias,&stat) && stat.offset==32);
    CHECK(sys_fstat(fd,(file_stat_t *)1)==(uint32)-1);
    CHECK(sys_link("/edge.txt","/edge.link")==0);
    CHECK(sys_unlink("/edge.txt")==0 && !sys_fstat(fd,&stat) && stat.nlink==1);
    CHECK(sys_unlink("/edge.link")==0 && !sys_fstat(fd,&stat) && !stat.nlink);
    CHECK(sys_lseek(fd,0,LSEEK_SET)==0 && sys_read(alias,9000,data)==9000);
    for(int i=0;i<9000;i++) CHECK(data[i]==(char)(i*7));
    uint32 number=stat.inode_num;
    CHECK(!sys_close(alias) && !sys_close(fd));
    fd=sys_open("/edge-reused",OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(fd!=(uint32)-1);
    CHECK(!sys_fstat(fd,&stat) && stat.inode_num==number);
    CHECK(!sys_close(fd) && !sys_unlink("/edge-reused"));
    CHECK(sys_open("/edge.txt",OPEN_READ)==(uint32)-1);

    fd=sys_open("/badelf",OPEN_CREATE|OPEN_READ|OPEN_WRITE); CHECK(fd!=(uint32)-1);
    CHECK(sys_write(fd,3,"bad")==3); CHECK(!sys_close(fd));
    char *args[]={"bad",NULL};
    volatile uint64 sentinel=0x1122334455667788ULL;
    CHECK(sys_exec("/badelf",args)==(uint32)-1 && sentinel==0x1122334455667788ULL);
    CHECK(sys_exec("/elfprobe",(char **)1)==(uint32)-1);
    char too_long[129]; memset(too_long,'a',128); too_long[128]=0;
    char *badargs[]={too_long,NULL};
    CHECK(sys_exec("/elfprobe",badargs)==(uint32)-1);
    char *many[34]; for(int i=0;i<33;i++) many[i]="x"; many[33]=NULL;
    CHECK(sys_exec("/elfprobe",many)==(uint32)-1);
    CHECK(sys_exec("/",args)==(uint32)-1);
    uint32 source=sys_open("/elfprobe",OPEN_READ); CHECK(source!=(uint32)-1);
    CHECK(!sys_fstat(source,&stat) && stat.size>=64 && stat.size<128*1024);
    char *image=(char *)sys_mmap(0,128*1024); CHECK((uint64)image!=0);
    CHECK(sys_read(source,stat.size,image)==stat.size);
    fd=sys_open("/badelf",OPEN_READ|OPEN_WRITE); CHECK(fd!=(uint32)-1);
    uint16 machine=*(uint16 *)(image+18); *(uint16 *)(image+18)=0;
    bad_image(fd,image,stat.size); *(uint16 *)(image+18)=machine;
    uint64 phoff=*(uint64 *)(image+32);
    uint16 phnum=*(uint16 *)(image+56);
    char *first=NULL,*second=NULL;
    for(uint32 i=0;i<phnum;i++) {
        char *ph=image+phoff+i*56; CHECK(ph+56<=image+stat.size);
        if(*(uint32 *)ph==1) { if(!first) first=ph; else if(!second) second=ph; }
    }
    CHECK(first && second && *(uint64 *)(first+32)>0);
    uint64 saved=*(uint64 *)(first+40);
    *(uint64 *)(first+40)=*(uint64 *)(first+32)-1;
    bad_image(fd,image,stat.size); *(uint64 *)(first+40)=saved;
    saved=*(uint64 *)(first+16); *(uint64 *)(first+16)=MMAP_BEGIN;
    bad_image(fd,image,stat.size); *(uint64 *)(first+16)=saved;
    uint32 flags=*(uint32 *)(first+4); *(uint32 *)(first+4)=2;
    bad_image(fd,image,stat.size); *(uint32 *)(first+4)=flags;
    saved=*(uint64 *)(second+16); *(uint64 *)(second+16)=*(uint64 *)(first+16);
    bad_image(fd,image,stat.size); *(uint64 *)(second+16)=saved;
    *(uint64 *)(image+32)=stat.size; bad_image(fd,image,stat.size);
    CHECK(!sys_close(source) && !sys_close(fd));
    CHECK(!sys_munmap((uint64)image,128*1024) && !sys_unlink("/badelf"));

    CHECK(!sys_mkdir("/edge_dir") && !sys_mkdir("/edge_dir/sub"));
    CHECK(sys_unlink("/edge_dir")== (uint32)-1);
    CHECK(sys_link("/edge_dir","/bad-dir-link")== (uint32)-1);
    CHECK(!sys_chdir("/edge_dir/sub") && !sys_print_cwd());
    CHECK(sys_unlink(".")== (uint32)-1 && sys_unlink("..")== (uint32)-1);
    CHECK(!sys_chdir("/") && !sys_unlink("/edge_dir/sub") && !sys_unlink("/edge_dir"));

    uint32 held[10]; int count=0;
    while(count<10 && (held[count]=sys_dup(STDOUT))!=(uint32)-1) count++;
    CHECK(count==7);
    CHECK(sys_open("/must-not-create",OPEN_CREATE|OPEN_WRITE)==(uint32)-1);
    for(int i=0;i<count;i++) CHECK(!sys_close(held[i]));
    CHECK(sys_open("/must-not-create",OPEN_READ)==(uint32)-1);
    for(int round=0;round<24;round++) {
        pid=sys_fork(); CHECK(pid>=0);
        if (!pid) {
            for(int i=0;i<7;i++) CHECK(sys_open("/ABCD.txt",OPEN_READ)!=(uint32)-1);
            sys_exit(0);
        }
        CHECK(sys_wait(&status)==(uint32)pid && !status);
    }
    CHECK(!sys_munmap((uint64)data,4*PGSIZE));
    fprintf(STDOUT,"LAB9_EDGE_OOM_BEGIN\n");
    pid=sys_fork(); CHECK(pid>=0);
    if (!pid) {
        uint64 base=sys_brk(0), top=base;
        while(sys_brk(top+1024*1024)!=(uint64)-1) top+=1024*1024;
        while(sys_brk(top+PGSIZE)!=(uint64)-1) top+=PGSIZE;
        CHECK(top>base && sys_exec("/elfprobe",args)==(uint32)-1);
        CHECK(sys_fork()==(uint32)-1);
        CHECK(sys_brk(base)==base);
        sys_exit(0);
    }
    CHECK(sys_wait(&status)==(uint32)pid && !status);
    fprintf(STDOUT,"LAB9_EDGE_PASS\n"); sys_exit(0);
}

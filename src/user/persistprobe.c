#include "help.h"
void main(int argc,char **argv)
{
    char bytes[16]; uint32 fd=sys_open("/persist.txt",OPEN_READ);
    if(fd==(uint32)-1 || sys_read(fd,sizeof(bytes),bytes)!=sizeof(bytes) ||
       strncmp(bytes,"LAB9_PERSIST_OK\n",16)) {
        fprintf(STDERR,"LAB9_COLD_FAIL\n"); sys_exit(1);
    }
    sys_close(fd); fprintf(STDOUT,"LAB9_COLD_PASS\n"); sys_exit(0);
}

#include "mod.h"
#include "../proc/mod.h"
static device_t devices[N_DEVICE];
static uint32 zero_read(uint32 len,uint64 dst,bool user)
{
    uint64 page=(uint64)pmem_alloc(true);
    if (!page) return -1;
    uint32 done=0;
    while (done<len) {
        uint32 cut=MIN(len-done,PGSIZE);
        if (user) { if (uvm_copyout(myproc()->pgtbl,dst+done,page,cut)<0) break; }
        else memmove((void *)(dst+done),(void *)page,cut);
        done+=cut;
    }
    pmem_free(page,true); return done || !len ? done : (uint32)-1;
}
static uint32 null_read(uint32 len,uint64 dst,bool user) { return 0; }
static uint32 null_write(uint32 len,uint64 src,bool user)
{
    char temp[64]; uint32 done=0;
    while (done<len) {
        uint32 cut=MIN(len-done,sizeof(temp));
        if (user && uvm_copyin(myproc()->pgtbl,(uint64)temp,src+done,cut)<0)
            return done ? done : (uint32)-1;
        done+=cut;
    }
    return done;
}
static uint32 error_write(uint32 len,uint64 src,bool user)
{
    printf("ERROR: "); return cons_write(len,src,user);
}
static uint32 gpt_write(uint32 len,uint64 src,bool user)
{
    if (!len) return 0;
    if (len>127) return -1;
    char tmp[128];
    if (user) { if (uvm_copyin(myproc()->pgtbl,(uint64)tmp,src,len)<0) return -1; }
    else memmove(tmp,(void *)src,len);
    tmp[len]=0;
    if (!strncmp(tmp,"Hello",6)) printf("Hi, I am gpt0!\n");
    else if (!strncmp(tmp,"Guess who I am",15)) printf("Your procid is %d and name is %s.\n",myproc()->pid,myproc()->name);
    else if (!strncmp(tmp,"How many free memory left",26)) {
        uint32 k,u; pmem_stat(&k,&u);
        printf("We have %d free pages in kernel space, %d free pages in user space!\n",k,u);
    } else if (!strncmp(tmp,"Good job",9)) printf("Thanks for your kind words!\n");
    else printf("Sorry, I can not understand it.\n");
    return len;
}
static void register_device(uint16 major,char *name,
    uint32 (*read)(uint32,uint64,bool),uint32 (*write)(uint32,uint64,bool))
{
    memmove(devices[major].name,name,strlen(name)+1);
    devices[major].read=read; devices[major].write=write;
    char path[80]="/dev/";
    memmove(path+5,name,strlen(name)+1);
    inode_t *ip=path_to_inode(path);
    if (!ip) ip=path_create_inode(path,INODE_TYPE_DIVICE,major,1);
    assert(ip!=NULL,"device_init: inode");
    inode_lock(ip);
    assert(ip->disk_info.type==INODE_TYPE_DIVICE && ip->disk_info.major==major,"device_init: conflicting inode");
    inode_unlock(ip); inode_put(ip);
}
void device_init(void)
{
    inode_t *dir=path_to_inode("/dev");
    if (!dir) dir=path_create_inode("/dev",INODE_TYPE_DIR,1,1);
    assert(dir!=NULL,"device_init: /dev");
    inode_lock(dir);
    assert(dir->disk_info.type==INODE_TYPE_DIR,"device_init: /dev type");
    inode_unlock(dir); inode_put(dir);
    register_device(2,"stdin",cons_read,NULL);
    register_device(3,"stdout",NULL,cons_write);
    register_device(4,"stderr",NULL,error_write);
    register_device(5,"zero",zero_read,NULL);
    register_device(6,"null",null_read,null_write);
    register_device(7,"gpt0",NULL,gpt_write);
}
bool device_open_check(uint16 major,uint32 mode)
{
    return major<N_DEVICE && devices[major].name[0] &&
        (!(mode&FILE_OPEN_READ) || devices[major].read!=NULL) &&
        (!(mode&FILE_OPEN_WRITE) || devices[major].write!=NULL);
}
uint32 device_read_data(uint16 major,uint32 len,uint64 dst,bool user)
{
    return major<N_DEVICE && devices[major].read ? devices[major].read(len,dst,user) : (uint32)-1;
}
uint32 device_write_data(uint16 major,uint32 len,uint64 src,bool user)
{
    return major<N_DEVICE && devices[major].write ? devices[major].write(len,src,user) : (uint32)-1;
}

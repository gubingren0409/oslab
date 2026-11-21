#include "mod.h"
#include "../proc/mod.h"
static file_t table[N_FILE];
static spinlock_t table_lock;
void file_init(void)
{
    spinlock_init(&table_lock,"file table");
    for (int i=0;i<N_FILE;i++) sleeplock_init(&table[i].io_lock,"file offset");
}
file_t *file_alloc(void)
{
    spinlock_acquire(&table_lock);
    for (int i=0;i<N_FILE;i++) if (!table[i].ref) {
        table[i].ref=1; table[i].ip=NULL; table[i].offset=0;
        table[i].readable=table[i].writbale=false;
        spinlock_release(&table_lock); return &table[i];
    }
    spinlock_release(&table_lock); return NULL;
}
file_t *file_dup(file_t *f)
{
    spinlock_acquire(&table_lock);
    assert(f && f->ref,"file_dup: ref"); f->ref++;
    spinlock_release(&table_lock); return f;
}
void file_close(file_t *f)
{
    spinlock_acquire(&table_lock);
    assert(f && f->ref,"file_close: ref");
    inode_t *ip=--f->ref ? NULL : f->ip;
    spinlock_release(&table_lock);
    inode_put(ip);
}
file_t *file_open(char *path, uint32 mode)
{
    if (!(mode&(FILE_OPEN_READ|FILE_OPEN_WRITE)) || (mode&~7u)) return NULL;
    file_t *f=file_alloc();
    if (!f) return NULL;
    inode_t *ip=path_to_inode(path);
    if (!ip && (mode&FILE_OPEN_CREATE)) ip=path_create_inode(path,INODE_TYPE_DATA,1,1);
    if (!ip) { file_close(f); return NULL; }
    inode_lock(ip);
    bool ok=ip->disk_info.type==INODE_TYPE_DATA ||
        (ip->disk_info.type==INODE_TYPE_DIR && !(mode&FILE_OPEN_WRITE)) ||
        (ip->disk_info.type==INODE_TYPE_DIVICE && device_open_check(ip->disk_info.major,mode));
    inode_unlock(ip);
    if (!ok) { inode_put(ip); file_close(f); return NULL; }
    f->ip=ip; f->readable=(mode&FILE_OPEN_READ)!=0; f->writbale=(mode&FILE_OPEN_WRITE)!=0;
    return f;
}
static uint32 file_io(file_t *f, uint32 len, uint64 addr, bool user, bool write)
{
    if (!f || !f->ip || (write ? !f->writbale : !f->readable)) return -1;
    sleeplock_acquire(&f->io_lock);
    inode_t *ip=f->ip; inode_lock(ip);
    uint32 result=(uint32)-1;
    switch (ip->disk_info.type) {
    case INODE_TYPE_DATA:
        result=write ? inode_write_data(ip,f->offset,len,(void *)addr,user) :
                       inode_read_data(ip,f->offset,len,(void *)addr,user);
        if (result!=(uint32)-1) f->offset+=result;
        break;
    case INODE_TYPE_DIR:
        if (!write) result=dentry_transmit(ip,addr,len,user);
        break;
    case INODE_TYPE_DIVICE:
        result=write ? device_write_data(ip->disk_info.major,len,addr,user) :
                       device_read_data(ip->disk_info.major,len,addr,user);
        break;
    }
    inode_unlock(ip); sleeplock_release(&f->io_lock);
    return result;
}
uint32 file_read(file_t *f,uint32 len,uint64 dst,bool user) { return file_io(f,len,dst,user,false); }
uint32 file_write(file_t *f,uint32 len,uint64 src,bool user) { return file_io(f,len,src,user,true); }
uint32 file_lseek(file_t *f,uint32 offset,uint32 flag)
{
    if (!f || !f->ip || flag>FILE_LSEEK_SUB) return -1;
    sleeplock_acquire(&f->io_lock); inode_lock(f->ip);
    uint32 result=(uint32)-1;
    if (f->ip->disk_info.type==INODE_TYPE_DATA) {
        uint64 next=flag==FILE_LSEEK_SET ? offset : flag==FILE_LSEEK_ADD ?
            (uint64)f->offset+offset : offset>f->offset ? 0 : f->offset-offset;
        if (next>f->ip->disk_info.size) next=f->ip->disk_info.size;
        result=f->offset=next;
    }
    inode_unlock(f->ip); sleeplock_release(&f->io_lock);
    return result;
}
uint32 file_get_stat(file_t *f,uint64 dst)
{
    if (!f || !f->ip) return -1;
    sleeplock_acquire(&f->io_lock); inode_lock(f->ip);
    file_stat_t stat={.type=f->ip->disk_info.type,.nlink=f->ip->disk_info.nlink,
        .size=f->ip->disk_info.size,.inode_num=f->ip->inode_num,.offset=f->offset};
    int result=uvm_copyout(myproc()->pgtbl,dst,(uint64)&stat,sizeof(stat));
    inode_unlock(f->ip); sleeplock_release(&f->io_lock);
    return result;
}

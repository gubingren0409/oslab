#include "mod.h"
#include "../fs/mod.h"

static file_t *get_fd(void)
{
    return arg_raw(0)<N_OPEN_FILE_PER_PROC ? myproc()->open_file[arg_raw(0)] : NULL;
}
static int empty_fd(void)
{
    for (int i=0;i<N_OPEN_FILE_PER_PROC;i++) if (!myproc()->open_file[i]) return i;
    return -1;
}
static bool user_range(uint64 addr,uint64 len,int perm)
{
    if (!len) return true;
    if (addr<USER_BASE || addr>=TRAPFRAME || len>TRAPFRAME-addr) return false;
    uint64 end=addr+len;
    for (uint64 page=PGROUNDDOWN(addr);page<end;page+=PGSIZE) {
        pte_t *pte=vm_getpte(myproc()->pgtbl,page,false);
        if (!pte || PTE_CHECK(*pte) || (*pte&(PTE_V|PTE_U|perm))!=(PTE_V|PTE_U|perm)) return false;
    }
    return true;
}
uint64 sys_exec(void)
{
    if (!fs_ready()) return -1;
    char path[STR_MAXLEN+1], *args[33];
    if (arg_str(0,path,sizeof(path))<0) return -1;
    char *storage=pmem_alloc(true);
    if (!storage) return -1;
    uint64 user_args=arg_raw(1);
    int result=-1;
    for (int i=0;i<=32;i++) {
        uint64 src=0;
        if (user_args && (user_args+i*sizeof(uint64)<user_args ||
            uvm_copyin(myproc()->pgtbl,(uint64)&src,user_args+i*sizeof(uint64),sizeof(src))<0)) goto done;
        args[i]=NULL;
        if (!src) { result=proc_exec(path,args); goto done; }
        if (i==32 || uvm_copyin_str(myproc()->pgtbl,(uint64)storage+i*128,src,128)<0) goto done;
        args[i]=storage+i*128;
    }
done:
    pmem_free((uint64)storage,true);
    return result;
}
uint64 sys_open(void)
{
    char path[STR_MAXLEN+1];
    int fd=empty_fd();
    if (!fs_ready() || fd<0 || arg_raw(1)>7 || arg_str(0,path,sizeof(path))<0) return -1;
    file_t *f=file_open(path,arg_raw(1));
    if (!f) return -1;
    myproc()->open_file[fd]=f; return fd;
}
uint64 sys_close(void)
{
    file_t *f=get_fd(); if (!f) return -1;
    myproc()->open_file[arg_raw(0)]=NULL; file_close(f); return 0;
}
uint64 sys_read(void)
{
    file_t *f=get_fd();
    if (!f || arg_raw(1)>0xffffffffUL || !user_range(arg_raw(2),arg_raw(1),PTE_W)) return 0;
    uint32 n=file_read(f,arg_raw(1),arg_raw(2),true);
    return n==(uint32)-1 ? 0 : n;
}
uint64 sys_write(void)
{
    file_t *f=get_fd();
    if (!f || arg_raw(1)>0xffffffffUL || !user_range(arg_raw(2),arg_raw(1),PTE_R)) return 0;
    uint32 n=file_write(f,arg_raw(1),arg_raw(2),true);
    return n==(uint32)-1 ? 0 : n;
}
uint64 sys_lseek(void)
{
    file_t *f=get_fd();
    if (!f || arg_raw(1)>0xffffffffUL || arg_raw(2)>2) return -1;
    uint32 result=file_lseek(f,arg_raw(1),arg_raw(2));
    return result==(uint32)-1 ? (uint64)-1 : result;
}
uint64 sys_dup(void)
{
    file_t *f=get_fd(); int fd=empty_fd();
    if (!f || fd<0) return -1;
    myproc()->open_file[fd]=file_dup(f); return fd;
}
uint64 sys_fstat(void)
{
    file_t *f=get_fd();
    if (!f || !user_range(arg_raw(1),sizeof(file_stat_t),PTE_W)) return -1;
    return (int)file_get_stat(f,arg_raw(1));
}
uint64 sys_get_dentries(void)
{
    file_t *f=get_fd();
    if (!f || !f->readable || arg_raw(2)>0xffffffffUL || !user_range(arg_raw(1),arg_raw(2),PTE_W)) return -1;
    inode_lock(f->ip);
    uint32 n=dentry_transmit(f->ip,arg_raw(1),arg_raw(2),true);
    inode_unlock(f->ip);
    return n==(uint32)-1 ? (uint64)-1 : n;
}
uint64 sys_mkdir(void)
{
    char path[STR_MAXLEN+1];
    if (!fs_ready() || arg_str(0,path,sizeof(path))<0) return -1;
    inode_t *ip=path_create_inode(path,INODE_TYPE_DIR,1,1);
    if (!ip) return -1;
    inode_put(ip); return 0;
}
uint64 sys_chdir(void)
{
    char path[STR_MAXLEN+1];
    if (!fs_ready() || arg_str(0,path,sizeof(path))<0) return -1;
    inode_t *ip=path_to_inode(path);
    if (!ip) return -1;
    inode_lock(ip);
    bool ok=ip->disk_info.type==INODE_TYPE_DIR;
    inode_unlock(ip);
    if (!ok) { inode_put(ip); return -1; }
    inode_t *old=myproc()->cwd;
    myproc()->cwd=ip; inode_put(old); return 0;
}
uint64 sys_print_cwd(void)
{
    char path[STR_MAXLEN+1];
    if (!fs_ready() || !myproc()->cwd) return -1;
    uint32 pos=inode_to_path(myproc()->cwd,path,sizeof(path));
    if (pos==(uint32)-1) return -1;
    printf("%s\n",path+pos); return 0;
}
uint64 sys_link(void)
{
    char oldpath[STR_MAXLEN+1],newpath[STR_MAXLEN+1];
    if (!fs_ready() || arg_str(0,oldpath,sizeof(oldpath))<0 || arg_str(1,newpath,sizeof(newpath))<0) return -1;
    return (int)path_link(oldpath,newpath);
}
uint64 sys_unlink(void)
{
    char path[STR_MAXLEN+1];
    if (!fs_ready() || arg_str(0,path,sizeof(path))<0) return -1;
    return (int)path_unlink(path);
}

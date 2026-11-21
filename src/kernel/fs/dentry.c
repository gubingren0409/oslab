#include "mod.h"
#include "../proc/mod.h"
static sleeplock_t namespace_lock;
void dentry_init(void) { sleeplock_init(&namespace_lock,"namespace"); }

static bool valid_name(char *name)
{
    if (!name || !*name) return false;
    for (int i=0;i<MAXLEN_FILENAME;i++) {
        if (!name[i]) return true;
        if (name[i]=='/') return false;
    }
    return false;
}
uint32 dentry_search(inode_t *ip, char *name)
{
    assert(sleeplock_holding(&ip->slk), "dentry_search: lock");
    if (ip->disk_info.type != INODE_TYPE_DIR || !valid_name(name) || !ip->disk_info.index[0]) return INVALID_INODE_NUM;
    buffer_t *b = buffer_get(ip->disk_info.index[0]);
    if (!b) return INVALID_INODE_NUM;
    uint32 result = INVALID_INODE_NUM;
    dentry_t *entries = (dentry_t *)b->data;
    for (int i=0;i<DENTRY_PER_BLOCK;i++)
        if (entries[i].name[0] && !strncmp(entries[i].name,name,MAXLEN_FILENAME)) { result=entries[i].inode_num; break; }
    buffer_put(b);
    return result;
}
uint32 dentry_create(inode_t *ip, uint32 num, char *name)
{
    assert(sleeplock_holding(&ip->slk), "dentry_create: lock");
    if (ip->disk_info.type != INODE_TYPE_DIR || !valid_name(name) ||
        dentry_search(ip,name) != INVALID_INODE_NUM || ip->disk_info.size >= BLOCK_SIZE) return -1;
    if (!ip->disk_info.index[0]) {
        uint32 block=bitmap_alloc_block();
        if (block==BLOCK_NUM_UNUSED) return -1;
        buffer_t *b=buffer_get(block);
        if (!b) { bitmap_free_block(block); return -1; }
        memset(b->data,0,BLOCK_SIZE); buffer_write(b); buffer_put(b);
        ip->disk_info.index[0]=block;
    }
    buffer_t *b=buffer_get(ip->disk_info.index[0]);
    if (!b) return -1;
    dentry_t *entries=(dentry_t *)b->data;
    uint32 result=(uint32)-1;
    for (int i=0;i<DENTRY_PER_BLOCK;i++) if (!entries[i].name[0]) {
        memset(&entries[i],0,sizeof(dentry_t));
        memmove(entries[i].name,name,strlen(name)+1);
        entries[i].inode_num=num;
        buffer_write(b); result=i*sizeof(dentry_t); break;
    }
    buffer_put(b);
    if (result!=(uint32)-1) { ip->disk_info.size+=sizeof(dentry_t); inode_rw(ip,true); }
    return result;
}
uint32 dentry_delete(inode_t *ip, char *name)
{
    assert(sleeplock_holding(&ip->slk), "dentry_delete: lock");
    if (ip->disk_info.type!=INODE_TYPE_DIR || !valid_name(name) || !ip->disk_info.index[0]) return INVALID_INODE_NUM;
    buffer_t *b=buffer_get(ip->disk_info.index[0]);
    if (!b) return INVALID_INODE_NUM;
    dentry_t *entries=(dentry_t *)b->data;
    uint32 result=INVALID_INODE_NUM;
    for (int i=0;i<DENTRY_PER_BLOCK;i++) if (entries[i].name[0] && !strncmp(entries[i].name,name,MAXLEN_FILENAME)) {
        result=entries[i].inode_num; memset(&entries[i],0,sizeof(dentry_t)); buffer_write(b); break;
    }
    buffer_put(b);
    if (result!=INVALID_INODE_NUM) { ip->disk_info.size-=sizeof(dentry_t); inode_rw(ip,true); }
    return result;
}
void dentry_print(inode_t *ip)
{
    assert(sleeplock_holding(&ip->slk), "dentry_print: lock");
    if (!ip->disk_info.index[0]) return;
    buffer_t *b=buffer_get(ip->disk_info.index[0]);
    if (!b) return;
    dentry_t *entries=(dentry_t *)b->data;
    for (int i=0;i<DENTRY_PER_BLOCK;i++)
        if (entries[i].name[0]) printf("dentry: offset=%d inode=%d name=%s\n",i*64,entries[i].inode_num,entries[i].name);
    buffer_put(b);
}
static inode_t *walk(char *path,char *name,bool parent)
{
    if (!path || !path[0]) return NULL;
    inode_t *ip=path[0]=='/' ? inode_get(ROOT_INODE) :
        (myproc()->cwd ? inode_dup(myproc()->cwd) : inode_get(ROOT_INODE));
    while (ip) {
        while (*path=='/') path++;
        if (!*path) {
            if (parent) { inode_put(ip); return NULL; }
            return ip;
        }
        int n=0;
        while (*path && *path!='/') {
            if (n==MAXLEN_FILENAME-1) { inode_put(ip); return NULL; }
            name[n++]=*path++;
        }
        name[n]=0;
        while (*path=='/') path++;
        inode_lock(ip);
        if (ip->disk_info.type!=INODE_TYPE_DIR) { inode_unlock(ip); inode_put(ip); return NULL; }
        if (parent && !*path) { inode_unlock(ip); return ip; }
        uint32 num=dentry_search(ip,name);
        inode_t *next= num!=INVALID_INODE_NUM ? inode_get(num) : NULL;
        inode_unlock(ip); inode_put(ip); ip=next;
    }
    return NULL;
}
inode_t *path_to_inode(char *path) { char name[MAXLEN_FILENAME]; return walk(path,name,false); }
inode_t *path_to_parent_inode(char *path,char *name) { return walk(path,name,true); }

uint32 dentry_search_2(inode_t *ip, uint32 num, char *name)
{
    assert(sleeplock_holding(&ip->slk),"dentry_search_2: lock");
    if (ip->disk_info.type!=INODE_TYPE_DIR || !ip->disk_info.index[0]) return -1;
    buffer_t *b=buffer_get(ip->disk_info.index[0]);
    if (!b) return -1;
    uint32 result=(uint32)-1;
    dentry_t *de=(dentry_t *)b->data;
    for (int i=0;i<DENTRY_PER_BLOCK;i++)
        if (de[i].name[0] && de[i].inode_num==num &&
            strncmp(de[i].name,".",2) && strncmp(de[i].name,"..",3)) {
            memmove(name,de[i].name,MAXLEN_FILENAME);
            name[MAXLEN_FILENAME-1]=0;
            result=i*sizeof(dentry_t); break;
        }
    buffer_put(b); return result;
}
uint32 dentry_transmit(inode_t *ip, uint64 dst, uint32 len, bool user)
{
    assert(sleeplock_holding(&ip->slk),"dentry_transmit: lock");
    if (ip->disk_info.type!=INODE_TYPE_DIR) return -1;
    if (!ip->disk_info.index[0]) return 0;
    buffer_t *b=buffer_get(ip->disk_info.index[0]);
    if (!b) return -1;
    dentry_t *de=(dentry_t *)b->data;
    uint32 done=0;
    for (int i=0;i<DENTRY_PER_BLOCK && len-done>=sizeof(*de);i++) if (de[i].name[0]) {
        if (user) {
            if (uvm_copyout(myproc()->pgtbl,dst+done,(uint64)&de[i],sizeof(*de))<0) {
                buffer_put(b); return done ? done : (uint32)-1;
            }
        } else memmove((void *)(dst+done),&de[i],sizeof(*de));
        done+=sizeof(*de);
    }
    buffer_put(b); return done;
}
uint32 inode_to_path(inode_t *ip, char *path, uint32 len)
{
    if (!ip || len<2) return -1;
    uint32 pos=len-1;
    path[pos]=0;
    inode_t *cur=inode_dup(ip);
    for (uint32 depth=0;cur->inode_num!=ROOT_INODE;depth++) {
        if (depth>=128) { inode_put(cur); return -1; }
        inode_lock(cur);
        uint32 num=cur->disk_info.type==INODE_TYPE_DIR ? dentry_search(cur,"..") : INVALID_INODE_NUM;
        inode_unlock(cur);
        inode_t *parent=num==INVALID_INODE_NUM ? NULL : inode_get(num);
        if (!parent || parent==cur) { inode_put(parent); inode_put(cur); return -1; }
        char name[MAXLEN_FILENAME];
        inode_lock(parent);
        uint32 found=dentry_search_2(parent,cur->inode_num,name);
        inode_unlock(parent);
        uint32 n=found==(uint32)-1 ? 0 : strlen(name);
        if (!n || pos<n+1) { inode_put(parent); inode_put(cur); return -1; }
        pos-=n; memmove(path+pos,name,n); path[--pos]='/';
        inode_put(cur); cur=parent;
    }
    inode_put(cur);
    if (pos==len-1) path[--pos]='/';
    return pos;
}
static bool special_name(char *name)
{
    return !strncmp(name,".",2) || !strncmp(name,"..",3);
}
inode_t *path_create_inode(char *path, uint16 type, uint16 major, uint16 minor)
{
    char name[MAXLEN_FILENAME];
    sleeplock_acquire(&namespace_lock);
    inode_t *parent=path_to_parent_inode(path,name), *ip=NULL;
    if (!parent || special_name(name)) goto done;
    inode_lock(parent);
    uint32 num=dentry_search(parent,name);
    if (num!=INVALID_INODE_NUM) {
        if (type==INODE_TYPE_DATA) ip=inode_get(num);
        inode_unlock(parent);
        if (ip) {
            inode_lock(ip);
            bool ok=ip->disk_info.type==INODE_TYPE_DATA;
            inode_unlock(ip);
            if (!ok) { inode_put(ip); ip=NULL; }
        }
        goto done;
    }
    ip=inode_create(type,major,minor);
    if (!ip) { inode_unlock(parent); goto done; }
    inode_lock(ip);
    bool ok=true;
    if (type==INODE_TYPE_DIR)
        ok=dentry_create(ip,ip->inode_num,".")!=(uint32)-1 &&
           dentry_create(ip,parent->inode_num,"..")!=(uint32)-1;
    if (ok) ok=dentry_create(parent,ip->inode_num,name)!=(uint32)-1;
    if (!ok) { ip->disk_info.nlink=0; inode_rw(ip,true); }
    inode_unlock(ip); inode_unlock(parent);
    if (!ok) { inode_put(ip); ip=NULL; }
done:
    inode_put(parent);
    sleeplock_release(&namespace_lock);
    return ip;
}
uint32 path_link(char *oldpath, char *newpath)
{
    char name[MAXLEN_FILENAME];
    uint32 result=(uint32)-1;
    sleeplock_acquire(&namespace_lock);
    inode_t *ip=path_to_inode(oldpath), *parent=path_to_parent_inode(newpath,name);
    if (!ip || !parent || special_name(name)) goto done;
    inode_lock(ip);
    if (ip->disk_info.type==INODE_TYPE_DIR || ip->disk_info.nlink<=0 || ip->disk_info.nlink==32767) {
        inode_unlock(ip); goto done;
    }
    inode_lock(parent);
    if (dentry_create(parent,ip->inode_num,name)!=(uint32)-1) {
        ip->disk_info.nlink++; inode_rw(ip,true); result=0;
    }
    inode_unlock(parent); inode_unlock(ip);
done:
    inode_put(parent); inode_put(ip); sleeplock_release(&namespace_lock);
    return result;
}
uint32 path_unlink(char *path)
{
    char name[MAXLEN_FILENAME];
    uint32 result=(uint32)-1;
    sleeplock_acquire(&namespace_lock);
    inode_t *parent=path_to_parent_inode(path,name), *ip=NULL;
    if (!parent || special_name(name)) goto done;
    inode_lock(parent);
    uint32 num=dentry_search(parent,name);
    if (num==INVALID_INODE_NUM || num==ROOT_INODE || num==parent->inode_num || !(ip=inode_get(num))) {
        inode_unlock(parent); goto done;
    }
    inode_lock(ip);
    bool empty=true;
    if (ip->disk_info.type==INODE_TYPE_DIR && ip->disk_info.index[0]) {
        buffer_t *b=buffer_get(ip->disk_info.index[0]);
        if (!b) empty=false;
        else {
            dentry_t *de=(dentry_t *)b->data;
            for (int i=0;i<DENTRY_PER_BLOCK;i++)
                if (de[i].name[0] && !special_name(de[i].name)) empty=false;
            buffer_put(b);
        }
    }
    if (empty && ip->disk_info.nlink>0 && dentry_delete(parent,name)==num) {
        ip->disk_info.nlink--; inode_rw(ip,true); result=0;
    }
    inode_unlock(ip); inode_unlock(parent);
done:
    inode_put(ip); inode_put(parent); sleeplock_release(&namespace_lock);
    return result;
}

#include "mod.h"

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
    if (!path || path[0]!='/') return NULL;
    inode_t *ip=inode_get(ROOT_INODE);
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

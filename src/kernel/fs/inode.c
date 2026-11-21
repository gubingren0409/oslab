#include "mod.h"
#include "../proc/mod.h"
extern super_block_t sb;
static inode_t cache[N_INODE];
static spinlock_t cache_lock;

void inode_init(void)
{
    spinlock_init(&cache_lock, "inode cache");
    for (int i = 0; i < N_INODE; i++) sleeplock_init(&cache[i].slk, "inode");
}
void inode_rw(inode_t *ip, bool write)
{
    assert(sleeplock_holding(&ip->slk), "inode_rw: lock");
    buffer_t *b = buffer_get(sb.inode_firstblock + ip->inode_num / INODE_PER_BLOCK);
    assert(b != NULL, "inode_rw: no buffer");
    void *disk = b->data + ip->inode_num % INODE_PER_BLOCK * sizeof(inode_disk_t);
    if (write) { memmove(disk, &ip->disk_info, sizeof(ip->disk_info)); buffer_write(b); }
    else memmove(&ip->disk_info, disk, sizeof(ip->disk_info));
    buffer_put(b);
}
inode_t *inode_get(uint32 num)
{
    if (num >= sb.total_inodes) return NULL;
    inode_t *free = NULL;
    spinlock_acquire(&cache_lock);
    for (int i = 0; i < N_INODE; i++) {
        inode_t *ip = &cache[i];
        if (ip->ref && ip->inode_num == num) {
            if (ip->deleting) { spinlock_release(&cache_lock); return NULL; }
            ip->ref++;
            spinlock_release(&cache_lock); return ip;
        }
        if (!ip->ref && !free) free = ip;
    }
    if (free) { free->ref = 1; free->inode_num = num; free->valid_info = false; free->deleting = false; }
    spinlock_release(&cache_lock);
    return free;
}
inode_t *inode_dup(inode_t *ip)
{
    spinlock_acquire(&cache_lock);
    assert(ip && ip->ref && !ip->deleting, "inode_dup: ref");
    ip->ref++;
    spinlock_release(&cache_lock);
    return ip;
}
void inode_lock(inode_t *ip)
{
    assert(ip && ip->ref, "inode_lock: ref");
    sleeplock_acquire(&ip->slk);
    if (!ip->valid_info) { inode_rw(ip, false); ip->valid_info = true; }
}
void inode_unlock(inode_t *ip) { sleeplock_release(&ip->slk); }
inode_t *inode_create(uint16 type, uint16 major, uint16 minor)
{
    if (type > INODE_TYPE_DIVICE) return NULL;
    uint32 num = bitmap_alloc_inode();
    if (num == BLOCK_NUM_UNUSED) return NULL;
    inode_t *ip = inode_get(num);
    if (!ip) { bitmap_free_inode(num); return NULL; }
    inode_lock(ip);
    memset(&ip->disk_info, 0, sizeof(ip->disk_info));
    ip->disk_info.type = type; ip->disk_info.major = major; ip->disk_info.minor = minor;
    ip->disk_info.nlink = 1;
    inode_rw(ip, true);
    inode_unlock(ip);
    return ip;
}
static void free_tree(uint32 block, int level)
{
    if (!block) return;
    if (level) {
        buffer_t *b = buffer_get(block);
        assert(b != NULL, "free_tree: buffer");
        uint32 *index = (uint32 *)b->data;
        for (int i = 0; i < BLOCK_SIZE / sizeof(uint32); i++) free_tree(index[i], level - 1);
        buffer_put(b);
    }
    bitmap_free_block(block);
}
void inode_delete(inode_t *ip)
{
    assert(sleeplock_holding(&ip->slk) && !ip->disk_info.nlink, "inode_delete: linked/lock");
    for (int i = 0; i < 13; i++) free_tree(ip->disk_info.index[i], i < 10 ? 0 : i < 12 ? 1 : 2);
    memset(&ip->disk_info, 0, sizeof(ip->disk_info));
    inode_rw(ip, true);
    bitmap_free_inode(ip->inode_num);
}
void inode_put(inode_t *ip)
{
    if (!ip) return;
    inode_lock(ip);
    spinlock_acquire(&cache_lock);
    bool remove = ip->ref == 1 && !ip->disk_info.nlink;
    if (remove) ip->deleting = true;
    spinlock_release(&cache_lock);
    if (remove) inode_delete(ip);
    inode_unlock(ip);
    spinlock_acquire(&cache_lock);
    assert(ip->ref > 0, "inode_put: ref");
    ip->ref--;
    if (remove) { ip->deleting = false; ip->valid_info = false; }
    spinlock_release(&cache_lock);
}

static uint32 new_block(void)
{
    uint32 block = bitmap_alloc_block();
    if (block == BLOCK_NUM_UNUSED) return 0;
    buffer_t *b = buffer_get(block);
    if (!b) { bitmap_free_block(block); return 0; }
    memset(b->data, 0, BLOCK_SIZE);
    buffer_write(b); buffer_put(b);
    return block;
}
/* Newly allocated index nodes are undone if a descendant cannot be allocated. */
static uint32 map_tree(uint32 *slot, int level, uint32 number, bool create)
{
    bool fresh = *slot == 0;
    if (fresh) {
        if (!create || !(*slot = new_block())) return 0;
    }
    if (!level) return *slot;
    buffer_t *b = buffer_get(*slot);
    if (!b) {
        if (fresh) { bitmap_free_block(*slot); *slot = 0; }
        return 0;
    }
    uint32 span = level == 2 ? 1024 : 1;
    uint32 *entry = &((uint32 *)b->data)[number / span];
    uint32 old = *entry;
    uint32 result = map_tree(entry, level - 1, number % span, create);
    if (old != *entry) buffer_write(b);
    buffer_put(b);
    if (!result && fresh) { bitmap_free_block(*slot); *slot = 0; }
    return result;
}
static uint32 locate(inode_t *ip, uint32 logical, bool create)
{
    if (logical < 10) return map_tree(&ip->disk_info.index[logical], 0, 0, create);
    logical -= 10;
    if (logical < 2048) return map_tree(&ip->disk_info.index[10 + logical / 1024], 1, logical % 1024, create);
    logical -= 2048;
    if (logical >= 1024 * 1024) return 0;
    return map_tree(&ip->disk_info.index[12], 2, logical, create);
}
uint32 inode_read_data(inode_t *ip, uint32 off, uint32 len, void *dst, bool user)
{
    assert(sleeplock_holding(&ip->slk), "inode_read: lock");
    if (off >= ip->disk_info.size) return 0;
    if (len > ip->disk_info.size - off) len = ip->disk_info.size - off;
    uint32 done = 0;
    while (done < len) {
        uint32 block = locate(ip, (off + done) / BLOCK_SIZE, false);
        if (!block) return done ? done : (uint32)-1;
        buffer_t *b = buffer_get(block);
        if (!b) return done ? done : (uint32)-1;
        uint32 pos = (off + done) % BLOCK_SIZE, cut = MIN(len - done, BLOCK_SIZE - pos);
        int result = 0;
        if (user) result = uvm_copyout(myproc()->pgtbl, (uint64)dst + done, (uint64)b->data + pos, cut);
        else memmove((char *)dst + done, b->data + pos, cut);
        buffer_put(b);
        if (result < 0) return done ? done : (uint32)-1;
        done += cut;
    }
    return done;
}
uint32 inode_write_data(inode_t *ip, uint32 off, uint32 len, void *src, bool user)
{
    assert(sleeplock_holding(&ip->slk), "inode_write: lock");
    if (off > ip->disk_info.size || (uint64)off + len > INODE_MAX_SIZE) return -1;
    if (!len) return 0;
    void *temp = user ? pmem_alloc(true) : NULL;
    if (user && !temp) return -1;
    uint32 done = 0;
    while (done < len) {
        uint32 pos = (off + done) % BLOCK_SIZE, cut = MIN(len - done, BLOCK_SIZE - pos);
        if (user && uvm_copyin(myproc()->pgtbl, (uint64)temp, (uint64)src + done, cut) < 0) break;
        uint32 block = locate(ip, (off + done) / BLOCK_SIZE, true);
        if (!block) break;
        buffer_t *b = buffer_get(block);
        if (!b) break;
        memmove(b->data + pos, user ? temp : (char *)src + done, cut);
        buffer_write(b); buffer_put(b);
        done += cut;
    }
    if (temp) pmem_free((uint64)temp, true);
    if (off + done > ip->disk_info.size) ip->disk_info.size = off + done;
    inode_rw(ip, true);
    return done ? done : (uint32)-1;
}
void inode_print(inode_t *ip, char *name)
{
    assert(sleeplock_holding(&ip->slk), "inode_print: lock");
    printf("inode %s: num=%d ref=%d type=%d nlink=%d size=%d\n", name, ip->inode_num,
           ip->ref, ip->disk_info.type, ip->disk_info.nlink, ip->disk_info.size);
    printf("index:");
    for (int i=0;i<13;i++) printf(" %d",ip->disk_info.index[i]);
    printf("\n");
}

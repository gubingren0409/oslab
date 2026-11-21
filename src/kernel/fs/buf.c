#include "mod.h"
#include "../proc/mod.h"

static buffer_node_t buf_cache[N_BUFFER];
static buffer_node_t active, inactive;
static spinlock_t cache_lock;
static void insert(buffer_node_t *n, buffer_node_t *head, bool front)
{
    if (n->next) { n->prev->next = n->next; n->next->prev = n->prev; }
    n->prev = front ? head : head->prev;
    n->next = front ? head->next : head;
    n->prev->next = n;
    n->next->prev = n;
}
void buffer_init(void)
{
    spinlock_init(&cache_lock, "buffer cache");
    active.next = active.prev = &active;
    inactive.next = inactive.prev = &inactive;
    for (int i = 0; i < N_BUFFER; i++) {
        buf_cache[i].buf.block_num = BLOCK_NUM_UNUSED;
        sleeplock_init(&buf_cache[i].buf.slk, "buffer");
        insert(&buf_cache[i], &inactive, false);
    }
}
static void disk_rw(buffer_t *b, bool write)
{
#ifdef VISIONFIVE2
    sdio_disk_rw(b, write);
#else
    virtio_disk_rw(b, write);
#endif
}
void buffer_write(buffer_t *b)
{
    assert(b && sleeplock_holding(&b->slk) && b->data, "buffer_write: lock/data");
    disk_rw(b, true);
    b->valid = true;
}
buffer_t *buffer_get(uint32 block)
{
    extern super_block_t sb;
    if (block == BLOCK_NUM_UNUSED || (sb.total_blocks && block >= sb.total_blocks)) return NULL;
    spinlock_acquire(&cache_lock);
    buffer_node_t *n;
    for (n = active.next; n != &active; n = n->next)
        if (n->buf.block_num == block) goto found;
    for (n = inactive.next; n != &inactive; n = n->next)
        if (n->buf.block_num == block) goto found;
    n = inactive.prev;
    if (n == &inactive) { spinlock_release(&cache_lock); return NULL; }
    n->buf.block_num = block;
    n->buf.valid = false;
    insert(n, &active, false);
    goto reserve;
found:
    if (sleeplock_holding(&n->buf.slk)) { spinlock_release(&cache_lock); return NULL; }
    insert(n, &active, true);
reserve:
    n->buf.ref++;
    spinlock_release(&cache_lock);
    buffer_t *b = &n->buf;
    sleeplock_acquire(&b->slk);
    if (!b->data) {
        b->data = pmem_alloc(true);
        if (!b->data) { buffer_freemem(1); b->data = pmem_alloc(true); }
        if (!b->data) { buffer_put(b); return NULL; }
    }
    if (!b->valid) { disk_rw(b, false); b->valid = true; }
    return b;
}
void buffer_put(buffer_t *b)
{
    assert(b && sleeplock_holding(&b->slk), "buffer_put: not held");
    sleeplock_release(&b->slk);
    spinlock_acquire(&cache_lock);
    assert(b->ref, "buffer_put: ref");
    if (--b->ref == 0) insert((buffer_node_t *)b, &inactive, true);
    spinlock_release(&cache_lock);
}
uint32 buffer_freemem(uint32 count)
{
    uint32 freed = 0;
    spinlock_acquire(&cache_lock);
    for (buffer_node_t *n = inactive.prev; n != &inactive && freed < count; n = n->prev) {
        if (n->buf.data) {
            pmem_free((uint64)n->buf.data, true);
            n->buf.data = NULL;
            n->buf.valid = false;
            freed++;
        }
    }
    spinlock_release(&cache_lock);
    return freed;
}
buffer_t *buffer_handle(uint64 handle)
{
    uint64 first = (uint64)buf_cache;
    if (handle < first || handle >= (uint64)(buf_cache + N_BUFFER) ||
        (handle - first) % sizeof(buffer_node_t)) return NULL;
    buffer_t *b = (buffer_t *)handle;
    return sleeplock_holding(&b->slk) ? b : NULL;
}
void buffer_release_owned(void)
{
    for (int i = 0; i < N_BUFFER; i++)
        if (sleeplock_holding(&buf_cache[i].buf.slk)) buffer_put(&buf_cache[i].buf);
}
void buffer_print_info(void)
{
    spinlock_acquire(&cache_lock);
    buffer_node_t *heads[] = { &active, &inactive };
    for (int i = 0; i < 2; i++) {
        printf("%s list:", i == 0 ? "active" : "inactive");
        int count = 0;
        for (buffer_node_t *n = heads[i]->next; n != heads[i]; n = n->next) {
            if (++count <= 24)
                printf(" %d(ref=%d,block=%d,data=%x)", (int)(n - buf_cache),
                       n->buf.ref, n->buf.block_num, (uint64)n->buf.data);
        }
        printf(" total=%d\n", count);
    }
    spinlock_release(&cache_lock);
}

#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;
// Keep duplicate-free detection O(1), including full user-pool reclamation.
// Both platforms retain the original 128 MiB managed physical interval.
static uint8 kern_allocated[(KERN_PAGES + 7) / 8];
static uint8 user_allocated[((128 * 1024 * 1024 / PGSIZE) + 7) / 8];

static void region_init(alloc_region_t *region, uint64 begin, uint64 end, char *name)
{
    region->begin = begin;
    region->end = end;
    region->allocable = 0;
    region->list_head.next = NULL;
    spinlock_init(&region->lk, name);
    uint8 *bits = region == &kern_region ? kern_allocated : user_allocated;
    uint64 pages = (end - begin) / PGSIZE;
    uint64 capacity = region == &kern_region ? KERN_PAGES : sizeof(user_allocated) * 8;
    assert(pages <= capacity, "pmem_init: allocation bitmap capacity");
    memset(bits, 0, (pages + 7) / 8);

    for (uint64 addr = begin; addr < end; addr += PGSIZE)
    {
        page_node_t *page = (page_node_t *)addr;
        memset(page, 1, PGSIZE);
        page->next = region->list_head.next;
        region->list_head.next = page;
        region->allocable++;
    }
}

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    uint64 alloc_begin = PGROUNDUP((uint64)ALLOC_BEGIN);
    uint64 alloc_end = PGROUNDDOWN((uint64)ALLOC_END);

    assert(alloc_begin < alloc_end, "pmem_init: empty allocation range");
    assert(KERN_PAGES > 0 &&
               KERN_PAGES < (alloc_end - alloc_begin) / PGSIZE,
           "pmem_init: regions must both be nonempty");

    uint64 kern_end = alloc_begin + KERN_PAGES * PGSIZE;

    region_init(&kern_region, alloc_begin, kern_end, "pmem-kernel");
    region_init(&user_region, kern_end, alloc_end, "pmem-user");
}

bool check_inkernel(uint64 p)
{
    return p >= kern_region.begin && p < kern_region.end;
}

uint32 pmem_available(bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    spinlock_acquire(&region->lk);
    uint32 count = region->allocable;
    spinlock_release(&region->lk);
    return count;
}

void pmem_stat(uint32 *kernel_pages,uint32 *user_pages)
{
    assert(kernel_pages && user_pages,"pmem_stat: output");
    spinlock_acquire(&kern_region.lk);
    spinlock_acquire(&user_region.lk);
    *kernel_pages=kern_region.allocable;
    *user_pages=user_region.allocable;
    spinlock_release(&user_region.lk);
    spinlock_release(&kern_region.lk);
}

// 尝试返回一个可分配的清零后的物理页
// 内存耗尽是可恢复的运行时条件，返回NULL。
void *pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    page_node_t *page;

    spinlock_acquire(&region->lk);
    if (region->allocable == 0)
    {
        assert(region->list_head.next == NULL,
               "pmem_alloc: inconsistent empty list");
        spinlock_release(&region->lk);
        return NULL;
    }

    assert(region->list_head.next != NULL,
           "pmem_alloc: inconsistent page count");

    page = region->list_head.next;
    uint32 index = ((uint64)page - region->begin) / PGSIZE;
    uint8 *bits = in_kernel ? kern_allocated : user_allocated;
    uint8 mask = 1u << (index % 8);
    assert(!(bits[index / 8] & mask), "pmem_alloc: free-list allocation mismatch");
    bits[index / 8] |= mask;
    region->list_head.next = page->next;
    region->allocable--;
    spinlock_release(&region->lk);

    memset(page, 0, PGSIZE);
    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;

    if (page % PGSIZE != 0)
        panic("pmem_free: unaligned page");
    if (page < region->begin || page >= region->end)
        panic("pmem_free: page out of range");

    spinlock_acquire(&region->lk);
    uint32 index = (page - region->begin) / PGSIZE;
    uint8 *bits = in_kernel ? kern_allocated : user_allocated;
    uint8 mask = 1u << (index % 8);
    if (!(bits[index / 8] & mask)) {
        spinlock_release(&region->lk);
        panic("pmem_free: duplicate free");
    }
    bits[index / 8] &= ~mask;

    page_node_t *node = (page_node_t *)page;
    memset(node, 1, PGSIZE);
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);
}

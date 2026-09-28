#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

static void region_init(alloc_region_t *region, uint64 begin, uint64 end, char *name)
{
    region->begin = begin;
    region->end = end;
    region->allocable = 0;
    region->list_head.next = NULL;
    spinlock_init(&region->lk, name);

    for (uint64 addr = begin; addr < end; addr += PGSIZE)
    {
        page_node_t *page = (page_node_t *)addr;
        page->next = region->list_head.next;
        region->list_head.next = page;
        region->allocable++;
    }
}

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    uint64 alloc_begin = (uint64)ALLOC_BEGIN;
    uint64 alloc_end = (uint64)ALLOC_END;
    uint64 kern_end = alloc_begin + KERN_PAGES * PGSIZE;

    assert(alloc_begin % PGSIZE == 0, "pmem_init: unaligned begin");
    assert(alloc_end % PGSIZE == 0, "pmem_init: unaligned end");
    assert(kern_end >= alloc_begin && kern_end <= alloc_end,
           "pmem_init: invalid kernel region");

    region_init(&kern_region, alloc_begin, kern_end, "pmem-kernel");
    region_init(&user_region, kern_end, alloc_end, "pmem-user");
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void *pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    page_node_t *page;

    spinlock_acquire(&region->lk);
    if (region->allocable == 0 || region->list_head.next == NULL)
    {
        spinlock_release(&region->lk);
        panic("pmem_alloc: out of memory");
    }

    page = region->list_head.next;
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
    for (page_node_t *node = region->list_head.next; node != NULL; node = node->next)
    {
        if ((uint64)node == page)
        {
            spinlock_release(&region->lk);
            panic("pmem_free: duplicate free");
        }
    }

    page_node_t *node = (page_node_t *)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);
}

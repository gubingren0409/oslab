#include "mod.h"

static mmap_region_t regions[N_MMAP];
static bool used[N_MMAP];
static spinlock_t pool_lock;

void mmap_init(void)
{
    memset(regions, 0, sizeof(regions));
    memset(used, 0, sizeof(used));
    spinlock_init(&pool_lock, "mmap pool");
}

mmap_region_t *mmap_region_alloc(void)
{
    spinlock_acquire(&pool_lock);
    for (int i = 0; i < N_MMAP; i++)
        if (!used[i])
        {
            used[i] = true;
            memset(&regions[i], 0, sizeof(regions[i]));
            spinlock_release(&pool_lock);
            return &regions[i];
        }
    spinlock_release(&pool_lock);
    return NULL;
}

void mmap_region_free(mmap_region_t *region)
{
    uint64 addr = (uint64)region;
    assert(addr >= (uint64)regions && addr < (uint64)(regions + N_MMAP) &&
               (addr - (uint64)regions) % sizeof(*region) == 0,
           "mmap_region_free: invalid node");
    int i = region - regions;
    spinlock_acquire(&pool_lock);
    assert(used[i], "mmap_region_free: duplicate free");
    used[i] = false;
    spinlock_release(&pool_lock);
}

static uint64 region_end(mmap_region_t *r)
{
    return r->begin + (uint64)r->npages * PGSIZE;
}

uint64 uvm_mmap_find(mmap_region_t *head, uint64 len, mmap_region_t **last, mmap_region_t **next)
{
    uint64 begin = MMAP_BEGIN;
    *last = NULL;
    for (*next = head; *next != NULL; *next = (*next)->next)
    {
        if (len <= (*next)->begin - begin)
            return begin;
        begin = region_end(*next);
        *last = *next;
    }
    return len <= MMAP_END - begin ? begin : 0;
}

uint64 uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    proc_t *p = myproc();
    uint64 len = (uint64)npages * PGSIZE;
    if (p == NULL || npages == 0 || len > MMAP_END - MMAP_BEGIN ||
        (begin % PGSIZE) || (perm & ~(PTE_R | PTE_W | PTE_X | PTE_U)) ||
        !(perm & (PTE_R | PTE_X)) || ((perm & PTE_W) && !(perm & PTE_R)))
        return 0;
    perm |= PTE_U;
    mmap_region_t *last = NULL, *next = p->mmap;
    if (begin == 0)
        begin = uvm_mmap_find(p->mmap, len, &last, &next);
    else
        while (next && next->begin < begin)
        {
            last = next;
            next = next->next;
        }
    if (begin < MMAP_BEGIN || begin >= MMAP_END || len > MMAP_END - begin ||
        (last && region_end(last) > begin) || (next && begin + len > next->begin))
        return 0;
    mmap_region_t *r = mmap_region_alloc();
    if (r == NULL)
        return 0;
    if (vm_user_alloc(p->pgtbl, begin, begin + len, perm) < 0)
    {
        mmap_region_free(r);
        return 0;
    }
    *r = (mmap_region_t){begin, npages, perm, next};
    if (last)
        last->next = r;
    else
        p->mmap = r;
    if (last && region_end(last) == begin && last->perm == perm)
    {
        last->npages += r->npages;
        last->next = next;
        mmap_region_free(r);
        r = last;
    }
    if (next && region_end(r) == next->begin && next->perm == perm)
    {
        r->npages += next->npages;
        r->next = next->next;
        mmap_region_free(next);
    }
    return begin;
}

int uvm_munmap(uint64 begin, uint32 npages)
{
    proc_t *p = myproc();
    uint64 len = (uint64)npages * PGSIZE;
    if (p == NULL || npages == 0 || begin % PGSIZE || begin < MMAP_BEGIN ||
        begin >= MMAP_END || len > MMAP_END - begin)
        return -1;
    uint64 end = begin + len, covered = begin;
    mmap_region_t *r = p->mmap;
    while (r && region_end(r) <= begin)
        r = r->next;
    mmap_region_t *first = r;
    for (; r && covered < end; r = r->next)
    {
        if (r->begin > covered)
            return -1;
        covered = MIN(end, region_end(r));
    }
    if (covered != end)
        return -1;
    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        pte_t *pte = vm_getpte(p->pgtbl, va, false);
        if (pte == NULL || (*pte & (PTE_V | PTE_U)) != (PTE_V | PTE_U))
            return -1;
    }
    mmap_region_t *split = NULL;
    if (begin > first->begin && end < region_end(first))
    {
        split = mmap_region_alloc();
        if (split == NULL)
            return -1;
    }
    vm_unmappages(p->pgtbl, begin, len, true);
    mmap_region_t **link = &p->mmap;
    while (*link && region_end(*link) <= begin)
        link = &(*link)->next;
    while ((r = *link) != NULL && r->begin < end)
    {
        uint64 old_end = region_end(r);
        if (r->begin < begin)
        {
            if (old_end > end)
            {
                *split = (mmap_region_t){end, (old_end - end) / PGSIZE, r->perm, r->next};
                r->next = split;
            }
            r->npages = (begin - r->begin) / PGSIZE;
            link = &r->next;
        }
        else if (old_end > end)
        {
            r->begin = end;
            r->npages = (old_end - end) / PGSIZE;
            break;
        }
        else
        {
            *link = r->next;
            mmap_region_free(r);
        }
    }
    sfence_vma();
    return 0;
}

void uvm_show_mmaplist(mmap_region_t *head)
{
    for (; head; head = head->next)
        printf("mmap %x - %x perm=%d\n", head->begin, region_end(head), head->perm);
}

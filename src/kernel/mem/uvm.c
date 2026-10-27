#include "mod.h"

static uint64 user_pa(pgtbl_t pgtbl, uint64 va, int permission)
{
    if (va < USER_BASE || va >= TRAPFRAME)
        return 0;
    pte_t *pte = vm_getpte(pgtbl, va, false);
    if (pte == NULL || (*pte & (PTE_V | PTE_U | permission)) !=
                       (PTE_V | PTE_U | permission) || PTE_CHECK(*pte))
        return 0;
    return PTE_TO_PA(*pte) + (va % PGSIZE);
}

static int user_copy(pgtbl_t pgtbl, uint64 kernel, uint64 user, uint32 len, bool out)
{
    if (len == 0)
        return 0;
    if (user >= TRAPFRAME || len > TRAPFRAME - user || kernel + len < kernel)
        return -1;
    while (len)
    {
        uint64 pa = user_pa(pgtbl, user, out ? PTE_W : PTE_R);
        if (pa == 0)
            return -1;
        uint32 n = MIN(len, PGSIZE - user % PGSIZE);
        if (out)
            memmove((void *)pa, (void *)kernel, n);
        else
            memmove((void *)kernel, (void *)pa, n);
        user += n;
        kernel += n;
        len -= n;
    }
    return 0;
}

int uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    return user_copy(pgtbl, dst, src, len, false);
}

int uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    return user_copy(pgtbl, src, dst, len, true);
}

int uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    for (uint32 i = 0; i < maxlen; i++)
    {
        if (src + i < src)
            return -1;
        uint64 pa = user_pa(pgtbl, src + i, PTE_R);
        if (pa == 0)
            return -1;
        char c = *(char *)pa;
        ((char *)dst)[i] = c;
        if (c == '\0')
            return 0;
    }
    return -1;
}

uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur, uint32 len)
{
    if (cur < USER_HEAP_BASE || cur > MMAP_BEGIN || len > MMAP_BEGIN - cur)
        return 0;
    uint64 end = cur + len;
    if (vm_user_alloc(pgtbl, PGROUNDUP(cur), PGROUNDUP(end), PTE_R | PTE_W | PTE_U) < 0)
        return 0;
    return end;
}

uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur, uint32 len)
{
    if (cur < USER_HEAP_BASE || cur > MMAP_BEGIN || len > cur - USER_HEAP_BASE)
        return 0;
    uint64 end = cur - len;
    if (PGROUNDUP(cur) > PGROUNDUP(end))
        vm_unmappages(pgtbl, PGROUNDUP(end), PGROUNDUP(cur) - PGROUNDUP(end), true);
    sfence_vma();
    return end;
}

int64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_npage, uint64 fault)
{
    if (old_npage == 0 || old_npage > (USER_STACK_TOP - MMAP_END) / PGSIZE)
        return -1;
    uint64 bottom = USER_STACK_TOP - old_npage * PGSIZE;
    if (fault < MMAP_END || fault >= bottom)
        return -1;
    uint64 next = PGROUNDDOWN(fault);
    if (vm_user_alloc(pgtbl, next, bottom, PTE_R | PTE_W | PTE_U) < 0)
        return -1;
    return (USER_STACK_TOP - next) / PGSIZE;
}

static void free_user_leaves(pgtbl_t pgtbl, int level)
{
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pgtbl[i];
        if (!(pte & PTE_V))
            continue;
        if (PTE_CHECK(pte))
        {
            assert(level > 0, "uvm_destroy: malformed table");
            free_user_leaves((pgtbl_t)PTE_TO_PA(pte), level - 1);
        }
        else if (pte & PTE_U)
        {
            pgtbl[i] = 0;
            pmem_free(PTE_TO_PA(pte), false);
        }
    }
}

void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    if (pgtbl != NULL)
    {
        free_user_leaves(pgtbl, 2);
        vm_freewalk(pgtbl);
    }
}

int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap, uint64 stack, mmap_region_t *mmap)
{
    (void)heap;
    (void)stack;
    (void)mmap;
    return vm_user_copy(old, new);
}

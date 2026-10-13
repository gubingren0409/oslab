#include "mod.h"

// in trap/trampoline.S
extern char trampoline[];

// 完整构造成功后才发布，避免其他hart观察到半成品页表。
static pgtbl_t kernel_pgtbl;

// vm_mappages事务期间用于标记本次新建的中间页表项；返回前一定清除。
#define PTE_SW_NEW (1ul << 8)
#define PHYS_ADDR_LIMIT (1ull << 56)

static pgtbl_t pte_child(pte_t pte, bool transaction)
{
    uint64 allowed = transaction ? (PTE_V | PTE_SW_NEW) : PTE_V;
    assert((pte & PTE_V) != 0 && PTE_CHECK(pte),
           "pte_child: not a pagetable");
    assert(PTE_FLAGS(pte) == PTE_V ||
               (transaction && PTE_FLAGS(pte) == allowed),
           "pte_child: invalid pagetable flags");

    pgtbl_t child = (pgtbl_t)PTE_TO_PA(pte);
    assert(child != NULL && ((uint64)child % PGSIZE) == 0,
           "pte_child: invalid child pagetable");
    return child;
}

// 根据pagetable找到va对应的最低层PTE。单次遍历中分配失败时，
// 只回收本次调用新建的中间页，不改变调用前的页表。
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if (pgtbl == NULL || va >= VA_MAX)
        return NULL;
    assert(((uint64)pgtbl % PGSIZE) == 0,
           "vm_getpte: unaligned pagetable");

    pte_t *created_slot[2];
    pgtbl_t created_page[2];
    int created = 0;

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pgtbl[VA_TO_VPN(va, level)];
        if (*pte & PTE_V)
        {
            pgtbl = pte_child(*pte, false);
            continue;
        }

        // V=0 但其余位非零的项可能由软件保留；不得当作空槽覆盖。
        if (*pte != 0)
            return NULL;

        if (!alloc)
            return NULL;

        pgtbl_t child = (pgtbl_t)pmem_alloc(true);
        if (child == NULL)
        {
            while (created > 0)
            {
                created--;
                *created_slot[created] = 0;
                pmem_free((uint64)created_page[created], true);
            }
            return NULL;
        }

        *pte = PA_TO_PTE(child) | PTE_V;
        created_slot[created] = pte;
        created_page[created] = child;
        created++;
        pgtbl = child;
    }

    return &pgtbl[VA_TO_VPN(va, 0)];
}

// 事务版本的页表遍历。新页通过RSW位做临时标记，由vm_finish_txn统一
// 提交或回滚，所以跨多个虚拟页的映射也能保持原子性。
static pte_t *vm_getpte_txn(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if (pgtbl == NULL || va >= VA_MAX)
        return NULL;

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pgtbl[VA_TO_VPN(va, level)];
        if (*pte & PTE_V)
        {
            pgtbl = pte_child(*pte, true);
            continue;
        }

        // 映射失败必须保持调用前所有页表项不变。
        if (*pte != 0)
            return NULL;

        if (!alloc)
            return NULL;

        pgtbl_t child = (pgtbl_t)pmem_alloc(true);
        if (child == NULL)
            return NULL;

        *pte = PA_TO_PTE(child) | PTE_V | PTE_SW_NEW;
        pgtbl = child;
    }

    return &pgtbl[VA_TO_VPN(va, 0)];
}

static bool vm_table_empty(pgtbl_t pgtbl)
{
    for (int i = 0; i < PGSIZE / (int)sizeof(pte_t); i++)
        if (pgtbl[i] & PTE_V)
            return false;
    return true;
}

static void vm_finish_txn(pgtbl_t pgtbl, bool commit)
{
    for (int i = 0; i < PGSIZE / (int)sizeof(pte_t); i++)
    {
        pte_t *pte = &pgtbl[i];
        if (!(*pte & PTE_V) || !PTE_CHECK(*pte))
            continue;

        pgtbl_t child = pte_child(*pte, true);
        vm_finish_txn(child, commit);

        if (*pte & PTE_SW_NEW)
        {
            if (commit)
            {
                *pte &= ~PTE_SW_NEW;
            }
            else
            {
                assert(vm_table_empty(child),
                       "vm_mappages: nonempty rollback table");
                *pte = 0;
                pmem_free((uint64)child, true);
            }
        }
    }
}

// 原子建立一段连续的4KiB映射。所有输入错误、重复映射和OOM均返回-1；
// 失败时叶项和本次新建的中间页表全部回滚。
int vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    const int allowed_perm = PTE_R | PTE_W | PTE_X | PTE_U;

    if (pgtbl == NULL || ((uint64)pgtbl % PGSIZE) != 0 ||
        (va % PGSIZE) != 0 || (pa % PGSIZE) != 0 || len == 0)
        return -1;
    if ((perm & ~allowed_perm) != 0 ||
        (perm & (PTE_R | PTE_X)) == 0 ||
        ((perm & PTE_W) != 0 && (perm & PTE_R) == 0))
        return -1;
    if (len > ~(uint64)0 - (PGSIZE - 1))
        return -1;

    uint64 span = PGROUNDUP(len);
    if (span == 0 || va >= VA_MAX || span > VA_MAX - va)
        return -1;
    if (pa >= PHYS_ADDR_LIMIT || span > PHYS_ADDR_LIMIT - pa)
        return -1;

    // 先检查整段，保证重复映射在进行任何分配前就失败。
    for (uint64 offset = 0; offset < span; offset += PGSIZE)
    {
        pte_t *pte = vm_getpte(pgtbl, va + offset, false);
        if (pte != NULL && *pte != 0)
        {
            if (*pte & PTE_V)
                assert(!PTE_CHECK(*pte),
                       "vm_mappages: level-0 pagetable");
            return -1;
        }
    }

    uint64 mapped = 0;
    for (; mapped < span; mapped += PGSIZE)
    {
        pte_t *pte = vm_getpte_txn(pgtbl, va + mapped, true);
        if (pte == NULL || *pte != 0)
            goto rollback;
        *pte = PA_TO_PTE(pa + mapped) | (uint64)perm | PTE_V;
    }

    vm_finish_txn(pgtbl, true);
    return 0;

rollback:
    for (uint64 offset = 0; offset < mapped; offset += PGSIZE)
    {
        pte_t *pte = vm_getpte_txn(pgtbl, va + offset, false);
        assert(pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte),
               "vm_mappages: lost rollback leaf");
        *pte = 0;
    }
    vm_finish_txn(pgtbl, false);
    return -1;
}

void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    assert(pgtbl != NULL && ((uint64)pgtbl % PGSIZE) == 0,
           "vm_unmappages: invalid pagetable");
    assert((va % PGSIZE) == 0 && len > 0,
           "vm_unmappages: invalid range");
    assert(len <= ~(uint64)0 - (PGSIZE - 1),
           "vm_unmappages: length overflow");

    uint64 span = PGROUNDUP(len);
    assert(va < VA_MAX && span > 0 && span <= VA_MAX - va,
           "vm_unmappages: range out of bounds");

    for (uint64 offset = 0; offset < span; offset += PGSIZE)
    {
        pte_t *pte = vm_getpte(pgtbl, va + offset, false);
        assert(pte != NULL && (*pte & PTE_V),
               "vm_unmappages: page not mapped");
        assert(!PTE_CHECK(*pte), "vm_unmappages: non-leaf pte");

        uint64 pa = PTE_TO_PA(*pte);
        *pte = 0;
        if (freeit)
            pmem_free(pa, check_inkernel(pa));
    }
}

static void vm_freewalk_level(pgtbl_t pgtbl, int level)
{
    for (int i = 0; i < PGSIZE / (int)sizeof(pte_t); i++)
    {
        pte_t *pte = &pgtbl[i];
        if (!(*pte & PTE_V))
            continue;

        if (PTE_CHECK(*pte))
        {
            assert(level > 0, "vm_freewalk: level-0 pagetable");
            pgtbl_t child = pte_child(*pte, false);
            *pte = 0;
            vm_freewalk_level(child, level - 1);
        }
        else
        {
            // 叶项的物理页归调用者所有，这里只拆除映射。
            *pte = 0;
        }
    }
    pmem_free((uint64)pgtbl, true);
}

void vm_freewalk(pgtbl_t pgtbl)
{
    if (pgtbl == NULL)
        return;
    assert(((uint64)pgtbl % PGSIZE) == 0,
           "vm_freewalk: unaligned pagetable");
    vm_freewalk_level(pgtbl, 2);
}

static void vm_print_level(pgtbl_t pgtbl, int level)
{
    for (int i = 0; i < PGSIZE / (int)sizeof(pte_t); i++)
    {
        pte_t pte = pgtbl[i];
        if (!(pte & PTE_V))
            continue;

        printf("level %d index %d pte %x pa %x\n", level, i,
               (uint64)pte, (uint64)PTE_TO_PA(pte));
        if (PTE_CHECK(pte))
        {
            assert(level > 0, "vm_print: level-0 pagetable");
            vm_print_level(pte_child(pte, false), level - 1);
        }
    }
}

void vm_print(pgtbl_t pgtbl)
{
    assert(pgtbl != NULL, "vm_print: null pagetable");
    printf("page table %x\n", (uint64)pgtbl);
    vm_print_level(pgtbl, 2);
}

void kvm_init(void)
{
    assert(kernel_pgtbl == NULL, "kvm_init: already initialized");

    pgtbl_t candidate = (pgtbl_t)pmem_alloc(true);
    if (candidate == NULL)
        panic("kvm_init: cannot allocate root");

    uint64 stack_page = 0;
    if (vm_mappages(candidate, UART_BASE, UART_BASE, PGSIZE,
                    PTE_R | PTE_W) < 0 ||
        vm_mappages(candidate, PLIC_BASE, PLIC_BASE, PLIC_SIZE,
                    PTE_R | PTE_W) < 0 ||
        vm_mappages(candidate, KERNEL_BASE, KERNEL_BASE,
                    (uint64)KERNEL_DATA - KERNEL_BASE,
                    PTE_R | PTE_X) < 0 ||
        vm_mappages(candidate, (uint64)KERNEL_DATA,
                    (uint64)KERNEL_DATA,
                    (uint64)ALLOC_END - (uint64)KERNEL_DATA,
                    PTE_R | PTE_W) < 0 ||
        vm_mappages(candidate, TRAMPOLINE, (uint64)trampoline,
                    PGSIZE, PTE_R | PTE_X) < 0)
        goto fail;

    stack_page = (uint64)pmem_alloc(true);
    if (stack_page == 0 ||
        vm_mappages(candidate, KSTACK(0), stack_page, PGSIZE,
                    PTE_R | PTE_W) < 0)
        goto fail;

    __sync_synchronize();
    kernel_pgtbl = candidate;
    return;

fail:
    vm_freewalk(candidate);
    if (stack_page != 0)
        pmem_free(stack_page, true);
    panic("kvm_init: cannot construct pagetable");
}

void kvm_inithart(void)
{
    assert(kernel_pgtbl != NULL, "kvm_inithart: no kernel pagetable");
    sfence_vma();
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

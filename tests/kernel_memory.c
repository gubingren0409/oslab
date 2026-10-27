#include "src/kernel/mem/mod.h"
#include "src/kernel/proc/mod.h"

static void *held_pages[KERN_PAGES];
static mmap_region_t *held_nodes[N_MMAP];

void memory_selftest(void)
{
    uint32 initial_kernel = pmem_available(true), initial_user = pmem_available(false);
    pgtbl_t root = pmem_alloc(true);
    assert(root != NULL, "test: root");
    assert(vm_user_alloc(root, PGSIZE, 3 * PGSIZE, PTE_R | PTE_W | PTE_U) == 0,
           "test: alloc two pages");
    char source[32] = "cross-page-copy", result[32];
    uint64 user = 2 * PGSIZE - 7;
    assert(uvm_copyout(root, user, (uint64)source, sizeof(source)) == 0,
           "test: copyout crossing page");
    assert(uvm_copyin(root, (uint64)result, user, sizeof(result)) == 0 &&
               strncmp(source, result, sizeof(result)) == 0, "test: copyin crossing page");
    assert(uvm_copyin_str(root, (uint64)result, user, 16) == 0 &&
               uvm_copyin_str(root, (uint64)result, user, 7) == -1,
           "test: string boundary");
    pte_t *readonly = vm_getpte(root, PGSIZE, false);
    *readonly &= ~PTE_W;
    assert(uvm_copyout(root, PGSIZE, (uint64)source, 1) == -1 &&
               uvm_copyin(root, (uint64)result, PGSIZE, 1) == 0,
           "test: readonly permission");
    assert(uvm_copyin(root, (uint64)result, ~(uint64)0, 2) == -1 &&
               uvm_copyout(root, 0, 0, 0) == 0, "test: invalid and zero length");

    pgtbl_t copy = pmem_alloc(true);
    uint64 tf = (uint64)pmem_alloc(false);
    assert(vm_mappages(copy, TRAPFRAME, tf, PGSIZE, PTE_R | PTE_W) == 0,
           "test: special mapping");
    assert(uvm_copyout(copy, TRAPFRAME, (uint64)source, 1) == -1,
           "test: special mapping protection");
    assert(uvm_copy_pgtbl(root, copy, 3 * PGSIZE, 0, NULL) == 0,
           "test: copy pagetable");
    assert(PTE_TO_PA(*vm_getpte(root, PGSIZE, false)) !=
               PTE_TO_PA(*vm_getpte(copy, PGSIZE, false)), "test: independent data");
    assert(uvm_copyin(copy, (uint64)result, user, sizeof(result)) == 0 &&
               strncmp(source, result, sizeof(result)) == 0, "test: cloned content");
    uvm_destroy_pgtbl(copy);
    pmem_free(tf, false);
    uvm_destroy_pgtbl(root);

    root = pmem_alloc(true);
    int count = 0;
    while ((held_pages[count] = pmem_alloc(true)) != NULL)
        count++;
    assert(count > 1, "test: kernel page reserve");
    pmem_free((uint64)held_pages[--count], true);
    uint32 before_user = pmem_available(false);
    assert(vm_user_alloc(root, PGSIZE, 2 * PGSIZE, PTE_R | PTE_W | PTE_U) == -1 &&
               root[0] == 0 && pmem_available(true) == 1 &&
               pmem_available(false) == before_user, "test: intermediate OOM rollback");
    while (count)
        pmem_free((uint64)held_pages[--count], true);
    uvm_destroy_pgtbl(root);

    proc_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.pgtbl = pmem_alloc(true);
    mycpu()->proc = &fake;
    uint64 begin = uvm_mmap(0, 3, PTE_R | PTE_W | PTE_U);
    assert(begin == MMAP_BEGIN, "test: first fit");
    count = 0;
    mmap_region_t *node;
    while ((node = mmap_region_alloc()) != NULL)
        held_nodes[count++] = node;
    assert(uvm_munmap(begin + PGSIZE, 1) == -1 && fake.mmap->npages == 3 &&
               (*vm_getpte(fake.pgtbl, begin + PGSIZE, false) & PTE_V),
           "test: split-node exhaustion rollback");
    while (count)
        mmap_region_free(held_nodes[--count]);
    assert(uvm_munmap(begin + PGSIZE, 1) == 0 &&
               uvm_munmap(begin, 3) == -1, "test: hole rejection");
    assert(uvm_mmap(begin + PGSIZE, 1, PTE_R | PTE_U) == begin + PGSIZE,
           "test: distinct permissions");
    assert(fake.mmap->next != NULL && fake.mmap->next->next != NULL,
           "test: do not merge different permissions");
    assert(uvm_munmap(begin, 3) == 0 && fake.mmap == NULL,
           "test: cross-region unmap");
    uvm_destroy_pgtbl(fake.pgtbl);
    mycpu()->proc = NULL;
    assert(pmem_available(true) == initial_kernel && pmem_available(false) == initial_user,
           "test: memory leak");
    printf("MEMORY_SELFTEST_PASS\n");
}

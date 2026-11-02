#include "src/kernel/mem/mod.h"
#include "src/kernel/proc/mod.h"
#include "src/kernel/trap/mod.h"
#include "src/kernel/lib/mod.h"

static void *held_pages[KERN_PAGES];
static mmap_region_t *held_nodes[N_MMAP];
static sleeplock_t test_sleep_lock;
static int sleep_lock_counter;

// 仅SELFTEST镜像通过私有调用63暴露，正式系统调用表不包含此入口。
uint64 memory_sleeplock_test(void)
{
    assert(!sleeplock_holding(&test_sleep_lock), "test: wrong sleep-lock owner");
    sleeplock_acquire(&test_sleep_lock);
    int previous = sleep_lock_counter;
    timer_wait(2);
    assert(sleeplock_holding(&test_sleep_lock) && previous == sleep_lock_counter,
           "test: sleep-lock exclusion");
    sleep_lock_counter++;
    sleeplock_release(&test_sleep_lock);
    return 0;
}

static volatile int nodes_allocated, nodes_checked, nodes_freed, nodes_restored;
static volatile uint64 timer_interrupts[NCPU];
static uint64 first_timer_time[NCPU];

// SELFTEST-only serial controls let the hardware runner check the PLIC RX path
// and return to U-Boot between images without changing persistent boot settings.
int memory_selftest_uart_control(int c)
{
    if (c == 'A')
        printf("UART_SELFTEST_PASS irq=%d hart=%d\n", UART_IRQ, mycpuid());
#ifdef VISIONFIVE2
    if (c == 'P')
    {
        vf2_reset_report();
        return 1;
    }
    if (c == '!')
        panic("VF2 reboot validation");
#endif
    return 0;
}

// Each real hart allocates and frees its own slice of the shared node pool.
// The barriers let the boot hart check exhaustion, uniqueness and full recovery.
void memory_selftest_hart(void)
{
    int active = NCPU - FIRST_HART;
    int index = mycpuid() - FIRST_HART;
    int begin = N_MMAP * index / active;
    int end = N_MMAP * (index + 1) / active;
    for (int i = begin; i < end; i++)
        held_nodes[i] = mmap_region_alloc();
    __sync_add_and_fetch(&nodes_allocated, 1);
    while (nodes_allocated != active)
        ;
    __sync_synchronize();

    if (mycpuid() == boot_hartid)
    {
        assert(mmap_region_alloc() == NULL, "test: concurrent node exhaustion");
        for (int i = 0; i < N_MMAP; i++)
        {
            assert(held_nodes[i] != NULL, "test: concurrent node allocation");
            for (int j = 0; j < i; j++)
                assert(held_nodes[i] != held_nodes[j], "test: duplicate node ownership");
        }
        __sync_synchronize();
        nodes_checked = 1;
    }
    while (!nodes_checked)
        ;
    __sync_synchronize();
    for (int i = begin; i < end; i++)
        mmap_region_free(held_nodes[i]);
    __sync_add_and_fetch(&nodes_freed, 1);
    while (nodes_freed != active)
        ;
    __sync_synchronize();

    if (mycpuid() == boot_hartid)
    {
        for (int i = 0; i < N_MMAP; i++)
        {
            held_nodes[i] = mmap_region_alloc();
            assert(held_nodes[i] != NULL, "test: concurrent node recovery");
        }
        assert(mmap_region_alloc() == NULL, "test: node pool cardinality");
        for (int i = 0; i < N_MMAP; i++)
            mmap_region_free(held_nodes[i]);
        printf("MMAP_CONCURRENCY_PASS harts=%d\n", active);
        __sync_synchronize();
        nodes_restored = 1;
    }
    while (!nodes_restored)
        ;
    __sync_synchronize();
}

// Called on actual timer interrupts, before the shared tick is advanced.
// Diagnostic output exists only in SELFTEST images.
void memory_selftest_timer_interrupt(void)
{
    int hart = mycpuid();
    uint64 count = __sync_add_and_fetch(&timer_interrupts[hart], 1);
    if (count == 1)
    {
        first_timer_time[hart] = r_time();
        printf("TIMER_HART_PASS hart=%d\n", hart);
    }
    if (hart != boot_hartid || count != 21)
        return;
    assert(timer_get_ticks() == 20, "test: shared ticks advance only once");
    for (int id = FIRST_HART; id < NCPU; id++)
        assert(timer_interrupts[id] >= 10, "test: every hart rearms its timer");
    uint64 delta = r_time() - first_timer_time[hart];
    assert(delta >= 20ul * INTERVAL && delta < 40ul * INTERVAL,
           "test: timer interval progression");
    printf("TIMER_SELFTEST_PASS ticks=20 interval=%d delta=%d\n",
           (int)INTERVAL, (int)delta);
}

void memory_selftest(void)
{
    sleeplock_init(&test_sleep_lock, "test sleeping mutex");
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

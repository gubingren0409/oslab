#include "mod.h"
#include "initcode.h"

extern char trampoline[];
extern void swtch(context_t *old, context_t *new);
extern void trap_user_return(void);

static proc_t proc_list[N_PROC];
static proc_t *proczero;
static int global_pid;
static spinlock_t pid_lk, wait_lk;

void proc_init(void)
{
    spinlock_init(&pid_lk, "pid");
    spinlock_init(&wait_lk, "wait");
    global_pid = 1;
    for (int i = 0; i < N_PROC; i++)
    {
        spinlock_init(&proc_list[i].lk, "process");
        proc_list[i].state = UNUSED;
        proc_list[i].kstack = KSTACK(i);
    }
}

pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    if (trapframe == 0 || trapframe % PGSIZE || check_inkernel(trapframe))
        return NULL;
    pgtbl_t table = pmem_alloc(true);
    if (table == NULL)
        return NULL;
    if (vm_mappages(table, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X) < 0 ||
        vm_mappages(table, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W) < 0)
    {
        vm_freewalk(table);
        return NULL;
    }
    return table;
}

void proc_return(void)
{
    proc_t *p = myproc();
    assert(p && spinlock_holding(&p->lk), "proc_return: no handoff lock");
    spinlock_release(&p->lk);
    trap_user_return();
    panic("proc_return: returned");
}

void proc_free(proc_t *p)
{
    assert(spinlock_holding(&p->lk) && p->state != RUNNING, "proc_free: unsafe state");
    uvm_destroy_pgtbl(p->pgtbl);
    if (p->tf != NULL)
        pmem_free((uint64)p->tf, false);
    while (p->mmap)
    {
        mmap_region_t *next = p->mmap->next;
        mmap_region_free(p->mmap);
        p->mmap = next;
    }
    p->pgtbl = NULL;
    p->tf = NULL;
    p->pid = 0;
    p->parent = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    p->sleep_space = NULL;
    p->exit_code = 0;
    p->name[0] = 0;
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->state = UNUSED;
}

proc_t *proc_alloc(void)
{
    for (int i = 0; i < N_PROC; i++)
    {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED)
        {
            spinlock_release(&p->lk);
            continue;
        }
        p->state = EMBRYO;
        spinlock_acquire(&pid_lk);
        if (global_pid == 0x7fffffff)
        {
            spinlock_release(&pid_lk);
            p->state = UNUSED;
            spinlock_release(&p->lk);
            return NULL;
        }
        p->pid = global_pid++;
        spinlock_release(&pid_lk);
        p->tf = pmem_alloc(false);
        if (p->tf != NULL)
            p->pgtbl = proc_pgtbl_init((uint64)p->tf);
        if (p->tf == NULL || p->pgtbl == NULL)
        {
            proc_free(p);
            spinlock_release(&p->lk);
            return NULL;
        }
        p->ctx.ra = (uint64)proc_return;
        p->ctx.sp = p->kstack + PGSIZE;
        memmove(p->name, "process", 8);
        return p;
    }
    return NULL;
}

void proc_make_first(void)
{
    assert(mycpuid() == boot_hartid && proczero == NULL, "proc_make_first: duplicate");
    assert(initcode_bin_len > 0 && initcode_bin_len <= PGSIZE, "proc_make_first: image size");
    proc_t *p = proc_alloc();
    if (p == NULL)
        panic("proc_make_first: out of memory");
    if (vm_user_alloc(p->pgtbl, USER_BASE, USER_BASE + PGSIZE, PTE_R | PTE_X | PTE_U) < 0 ||
        vm_user_alloc(p->pgtbl, USER_STACK_TOP - PGSIZE, USER_STACK_TOP, PTE_R | PTE_W | PTE_U) < 0)
    {
        proc_free(p);
        spinlock_release(&p->lk);
        panic("proc_make_first: mapping failed");
    }
    pte_t *code = vm_getpte(p->pgtbl, USER_BASE, false);
    memmove((void *)PTE_TO_PA(*code), initcode_bin, initcode_bin_len);
    asm volatile("fence.i" ::: "memory");
    p->heap_top = USER_HEAP_BASE;
    p->ustack_npage = 1;
    p->tf->user_to_kern_epc = USER_BASE;
    p->tf->sp = USER_STACK_TOP;
    memmove(p->name, "proczero", 9);
    proczero = p;
    p->state = RUNNABLE;
    spinlock_release(&p->lk);
}

int proc_fork(void)
{
    proc_t *parent = myproc();
    proc_t *child = proc_alloc();
    if (child == NULL)
        return -1;
    if (uvm_copy_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top,
                       parent->ustack_npage, parent->mmap) < 0)
        goto fail;
    mmap_region_t **link = &child->mmap;
    for (mmap_region_t *r = parent->mmap; r != NULL; r = r->next)
    {
        mmap_region_t *node = mmap_region_alloc();
        if (node == NULL)
            goto fail;
        *node = *r;
        node->next = NULL;
        *link = node;
        link = &node->next;
    }
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;
    *child->tf = *parent->tf;
    child->tf->a0 = 0;
    memmove(child->name, parent->name, sizeof(child->name));
    int pid = child->pid;

    // EMBRYO 不可调度；先释放它的锁，再按 wait_lock -> proc.lock 发布。
    spinlock_release(&child->lk);
    spinlock_acquire(&wait_lk);
    spinlock_acquire(&child->lk);
    child->parent = parent;
    child->state = RUNNABLE;
    spinlock_release(&child->lk);
    spinlock_release(&wait_lk);
    return pid;
fail:
    proc_free(child);
    spinlock_release(&child->lk);
    return -1;
}

void proc_sched(void)
{
    proc_t *p = myproc();
    cpu_t *c = mycpu();
    assert(p && spinlock_holding(&p->lk), "proc_sched: process lock");
    assert(c->noff == 1 && !intr_get() && p->state != RUNNING,
           "proc_sched: unsafe interrupt/state");
    int enabled = c->intena;
    swtch(&p->ctx, &c->ctx);
    // 进程可能在另一 hart 上恢复，不得继续写先前缓存的 CPU 对象。
    mycpu()->intena = enabled;
}

void proc_yield(void)
{
    proc_t *p = myproc();
    if (p == NULL)
        return;
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched();
    spinlock_release(&p->lk);
}

void proc_scheduler(void)
{
    cpu_t *c = mycpu();
    c->proc = NULL;
    while (1)
    {
        intr_on();
        bool found = false;
        for (int i = 0; i < N_PROC; i++)
        {
            proc_t *p = &proc_list[i];
            spinlock_acquire(&p->lk);
            if (p->state == RUNNABLE)
            {
                found = true;
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->ctx, &p->ctx);
                c->proc = NULL;
            }
            spinlock_release(&p->lk);
        }
        if (!found)
            asm volatile("wfi");
    }
}

void proc_sleep(void *channel, spinlock_t *lock)
{
    proc_t *p = myproc();
    assert(p && lock != &p->lk && spinlock_holding(lock), "proc_sleep: invalid condition lock");
    spinlock_acquire(&p->lk);
    spinlock_release(lock);
    p->sleep_space = channel;
    p->state = SLEEPING;
    proc_sched();
    p->sleep_space = NULL;
    spinlock_release(&p->lk);
    spinlock_acquire(lock);
}

void proc_wakeup(void *channel)
{
    proc_t *current = myproc();
    for (int i = 0; i < N_PROC; i++)
    {
        proc_t *p = &proc_list[i];
        if (p == current)
            continue;
        spinlock_acquire(&p->lk);
        if (p->state == SLEEPING && p->sleep_space == channel)
            p->state = RUNNABLE;
        spinlock_release(&p->lk);
    }
}

static void proc_try_wakeup(proc_t *p)
{
    if (p == NULL || p == myproc())
        return;
    spinlock_acquire(&p->lk);
    if (p->state == SLEEPING && p->sleep_space == p)
        p->state = RUNNABLE;
    spinlock_release(&p->lk);
}

static void proc_reparent(proc_t *parent)
{
    for (int i = 0; i < N_PROC; i++)
        if (proc_list[i].parent == parent)
            proc_list[i].parent = proczero;
    proc_try_wakeup(proczero);
}

void proc_exit(int status)
{
    proc_t *p = myproc();
    spinlock_acquire(&wait_lk);
    proc_reparent(p);
    proc_try_wakeup(p->parent);
    spinlock_acquire(&p->lk);
    p->exit_code = status;
    p->state = ZOMBIE;
    spinlock_release(&wait_lk);
    proc_sched();
    panic("proc_exit: resumed");
}

int proc_wait(uint64 user_addr)
{
    proc_t *parent = myproc();
    spinlock_acquire(&wait_lk);
    while (1)
    {
        bool have_child = false;
        for (int i = 0; i < N_PROC; i++)
        {
            proc_t *p = &proc_list[i];
            if (p == proczero || p->parent != parent)
                continue;
            spinlock_acquire(&p->lk);
            have_child = true;
            if (p->state == ZOMBIE)
            {
                if (user_addr && uvm_copyout(parent->pgtbl, user_addr,
                                             (uint64)&p->exit_code, sizeof(p->exit_code)) < 0)
                {
                    spinlock_release(&p->lk);
                    spinlock_release(&wait_lk);
                    return -1;
                }
                int pid = p->pid;
                proc_free(p);
                spinlock_release(&p->lk);
                spinlock_release(&wait_lk);
                return pid;
            }
            spinlock_release(&p->lk);
        }
        if (!have_child)
        {
            spinlock_release(&wait_lk);
            return -1;
        }
        proc_sleep(parent, &wait_lk);
    }
}

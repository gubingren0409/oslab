#include "mod.h"

uint64 sys_brk(void)
{
    proc_t *p = myproc();
    uint64 next = arg_raw(0), old = p->heap_top;
    if (next == 0)
        return old;
    if (next < USER_HEAP_BASE || next > MMAP_BEGIN)
        return -1;
    uint64 diff = next > old ? next - old : old - next;
    if (diff > 0xfffffffful)
        return -1;
    uint64 result = next >= old ? uvm_heap_grow(p->pgtbl, old, diff) :
                                 uvm_heap_ungrow(p->pgtbl, old, diff);
    if (result == 0)
        return -1;
    p->heap_top = result;
    return result;
}

uint64 sys_mmap(void)
{
    uint64 begin = arg_raw(0), len = arg_raw(1);
    if (len == 0 || len > 0xfffffffful || len % PGSIZE || begin % PGSIZE)
        return 0;
    return uvm_mmap(begin, len / PGSIZE, PTE_R | PTE_W | PTE_U);
}

uint64 sys_munmap(void)
{
    uint64 begin = arg_raw(0), len = arg_raw(1);
    if (len == 0 || len > 0xfffffffful || len % PGSIZE || begin % PGSIZE)
        return -1;
    return uvm_munmap(begin, len / PGSIZE);
}


uint64 sys_print_str(void)
{
    char buf[STR_MAXLEN];
    if (arg_str(0, buf, sizeof(buf)) < 0)
        return -1;
    printf("%s", buf);
    return 0;
}

uint64 sys_print_int(void)
{
    printf("%d", (int)arg_raw(0));
    return 0;
}

uint64 sys_getpid(void)
{
    return myproc()->pid;
}

uint64 sys_fork(void)
{
    return proc_fork();
}

uint64 sys_wait(void)
{
    return proc_wait(arg_raw(0));
}

uint64 sys_exit(void)
{
    proc_exit((int)arg_raw(0));
}

uint64 sys_sleep(void)
{
    timer_wait(arg_raw(0));
    return 0;
}

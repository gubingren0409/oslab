#include "mod.h"

uint64 sys_helloworld(void)
{
    printf("proczero: hello world!\n");
    return 0;
}

uint64 sys_copyin(void)
{
    uint64 addr = arg_raw(0), count = arg_raw(1);
    if (count > 0xfffffffful / sizeof(int) || addr >= TRAPFRAME ||
        count * sizeof(int) > TRAPFRAME - addr)
        return -1;
    for (uint64 i = 0; i < count; i++)
    {
        int value;
        if (uvm_copyin(myproc()->pgtbl, (uint64)&value, addr + i * sizeof(int), sizeof(value)) < 0)
            return -1;
        printf("%d ", value);
    }
    printf("\n");
    return 0;
}

uint64 sys_copyout(void)
{
    int values[] = {1, 2, 3, 4, 5};
    return uvm_copyout(myproc()->pgtbl, arg_raw(0), (uint64)values, sizeof(values)) < 0 ?
           (uint64)-1 : 5;
}

uint64 sys_copyinstr(void)
{
    char buf[STR_MAXLEN];
    if (arg_str(0, buf, sizeof(buf)) < 0)
        return -1;
    printf("%s\n", buf);
    return 0;
}

uint64 sys_printf(void)
{
    char buf[STR_MAXLEN];
    if (arg_str(0, buf, sizeof(buf)) < 0)
        return -1;
    printf("%s", buf);
    return 0;
}

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

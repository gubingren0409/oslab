#include "mod.h"
#ifdef OSLAB_SELFTEST
extern uint64 memory_sleeplock_test(void);
#endif

static uint64 (*const syscalls[])(void) = {
    [SYS_brk] = sys_brk,
    [SYS_mmap] = sys_mmap,
    [SYS_munmap] = sys_munmap,
    [SYS_print_str] = sys_print_str,
    [SYS_print_int] = sys_print_int,
    [SYS_getpid] = sys_getpid,
    [SYS_fork] = sys_fork,
    [SYS_wait] = sys_wait,
    [SYS_exit] = sys_exit,
    [SYS_sleep] = sys_sleep,
    [SYS_alloc_block] = sys_alloc_block,
    [SYS_free_block] = sys_free_block,
    [SYS_alloc_inode] = sys_alloc_inode,
    [SYS_free_inode] = sys_free_inode,
    [SYS_show_bitmap] = sys_show_bitmap,
    [SYS_get_block] = sys_get_block,
    [SYS_read_block] = sys_read_block,
    [SYS_write_block] = sys_write_block,
    [SYS_put_block] = sys_put_block,
    [SYS_show_buffer] = sys_show_buffer,
    [SYS_flush_buffer] = sys_flush_buffer,
#ifdef OSLAB_SELFTEST
    [63] = memory_sleeplock_test,
#endif
};

void syscall(void)
{
    proc_t *p = myproc();
    assert(p && p->tf, "syscall: no process");
    uint64 n = p->tf->a7;
    p->tf->a0 = n < sizeof(syscalls) / sizeof(syscalls[0]) && syscalls[n] ?
                syscalls[n]() : (uint64)-1;
}

uint64 arg_raw(int n)
{
    proc_t *p = myproc();
    assert(p && p->tf && n >= 0 && n < 6, "arg_raw: invalid context/index");
    return (&p->tf->a0)[n];
}

void arg_uint32(int n, uint32 *value)
{
    assert(value != NULL, "arg_uint32: null output");
    *value = arg_raw(n);
}

void arg_uint64(int n, uint64 *value)
{
    assert(value != NULL, "arg_uint64: null output");
    *value = arg_raw(n);
}

int arg_str(int n, char *buf, int maxlen)
{
    return maxlen > 0 ? uvm_copyin_str(myproc()->pgtbl, (uint64)buf, arg_raw(n), maxlen) : -1;
}

#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "initcode.h"
#define initcode initcode_bin
#define initcode_len initcode_bin_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return(void);

// 第一个用户进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成trapframe和trampoline的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    if (trapframe == 0 || (trapframe % PGSIZE) != 0 ||
        check_inkernel(trapframe))
        return NULL;

    assert(((uint64)trampoline % PGSIZE) == 0,
           "proc_pgtbl_init: unaligned trampoline");

    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    if (pgtbl == NULL)
        return NULL;

    if (vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline,
                    PGSIZE, PTE_R | PTE_X) < 0 ||
        vm_mappages(pgtbl, TRAPFRAME, trapframe,
                    PGSIZE, PTE_R | PTE_W) < 0)
    {
        vm_freewalk(pgtbl);
        return NULL;
    }

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

	注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
    assert(mycpuid() == boot_hartid, "proc_make_first: not boot hart");
    assert(myproc() == NULL && proczero.pgtbl == NULL,
           "proc_make_first: already initialized");
    assert(initcode_len > 0 && initcode_len <= PGSIZE,
           "proc_make_first: invalid initcode");

    proc_t first;
    memset(&first, 0, sizeof(first));

    uint64 trapframe = (uint64)pmem_alloc(false);
    uint64 user_stack = 0;
    uint64 user_code = 0;

    if (trapframe == 0)
        goto fail;

    first.pgtbl = proc_pgtbl_init(trapframe);
    if (first.pgtbl == NULL)
        goto fail;

    user_stack = (uint64)pmem_alloc(false);
    user_code = (uint64)pmem_alloc(false);
    if (user_stack == 0 || user_code == 0)
        goto fail;

    if (vm_mappages(first.pgtbl, TRAPFRAME - PGSIZE, user_stack,
                    PGSIZE, PTE_R | PTE_W | PTE_U) < 0 ||
        vm_mappages(first.pgtbl, USER_BASE, user_code,
                    PGSIZE, PTE_R | PTE_X | PTE_U) < 0)
        goto fail;

    memmove((void *)user_code, initcode, initcode_len);
    // 用户代码刚由数据写入生成；进入 U-mode 前同步本 hart 的取指视图。
    asm volatile("fence.i" ::: "memory");

    first.pid = 0;
    first.heap_top = USER_BASE + PGSIZE;
    first.ustack_npage = 1;
    first.tf = (trapframe_t *)trapframe;
    first.kstack = KSTACK(0);
    first.tf->user_to_kern_epc = USER_BASE;
    first.tf->sp = TRAPFRAME;
    first.ctx.ra = (uint64)trap_user_return;
    first.ctx.sp = KSTACK(0) + PGSIZE;

    // 一次性发布完整PCB，再切换到进程的内核栈和首次返回入口。
    proczero = first;
    __sync_synchronize();
    cpu_t *cpu = mycpu();
    cpu->proc = &proczero;

    intr_off();
    swtch(&cpu->ctx, &proczero.ctx);
    panic("proc_make_first: unexpected return");

fail:
    if (first.pgtbl != NULL)
        vm_freewalk(first.pgtbl);
    if (user_code != 0)
        pmem_free(user_code, false);
    if (user_stack != 0)
        pmem_free(user_stack, false);
    if (trapframe != 0)
        pmem_free(trapframe, false);
    panic("proc_make_first: out of memory");

}

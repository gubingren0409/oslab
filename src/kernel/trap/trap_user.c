#include "mod.h"
#include "../../user/syscall_num.h"
#include "../syscall/mod.h"

// in trampoline.S
extern char trampoline[];
extern char user_vector[];
extern char user_return[];

// in trap.S
extern char kernel_vector[];

#define SCAUSE_INTERRUPT (1ull << 63)

static const char *user_exception_name(uint64 cause)
{
    static const char *const names[16] = {
        "instruction address misaligned",
        "instruction access fault",
        "illegal instruction",
        "breakpoint",
        "load address misaligned",
        "load access fault",
        "store/AMO address misaligned",
        "store/AMO access fault",
        "environment call from U-mode",
        "environment call from S-mode",
        "reserved",
        "environment call from M-mode",
        "instruction page fault",
        "load page fault",
        "reserved",
        "store/AMO page fault",
    };

    return cause < 16 ? names[cause] : "unknown exception";
}

static __attribute__((noreturn)) void user_fault_stop(proc_t *proc,
                                                       uint64 scause,
                                                       uint64 sepc,
                                                       uint64 stval)
{
    uint64 cause = scause & ~SCAUSE_INTERRUPT;
    printf("\nuser exception: %s\n", user_exception_name(cause));
    printf("pid = %d, scause = %x, sepc = %x, stval = %x\n",
           proc->pid, scause, sepc, stval);

    // Lab4尚无调度器：撤下故障进程并让本hart停在可处理中断的内核循环。
    // 这样页错误不会重放，也不会把整个内核带入panic。
    mycpu()->proc = NULL;
    __sync_synchronize();
    intr_on();
    while (1)
        asm volatile("wfi");
}

// 在user_vector切回内核页表后处理用户态trap。
void trap_user_handler(void)
{
    // 第一时间恢复内核 trap 向量，避免后续任何 S-mode trap 重入用户入口。
    w_stvec((uint64)kernel_vector);

    uint64 sstatus = r_sstatus();
    assert((sstatus & SSTATUS_SPP) == 0,
           "trap_user_handler: not from user mode");
    assert(intr_get() == 0,
           "trap_user_handler: interrupt enabled");

    proc_t *proc = myproc();
    assert(proc != NULL && proc->tf != NULL,
           "trap_user_handler: no current process");

    uint64 scause = r_scause();
    uint64 sepc = r_sepc();
    uint64 stval = r_stval();
    uint64 cause = scause & ~SCAUSE_INTERRUPT;
    proc->tf->user_to_kern_epc = sepc;

    if (scause & SCAUSE_INTERRUPT)
    {
        if (cause == 5)
            timer_interrupt_handler();
        else if (cause == 9)
            external_interrupt_handler();
        else
            user_fault_stop(proc, scause, sepc, stval);
    }
    else if (cause == 8)
    {
        // ecall自身不会自动前进sepc；必须跳过它，避免返回后重复调用。
        proc->tf->user_to_kern_epc = sepc + 4;
        syscall();
    }
    else if (cause == 13 || cause == 15)
    {
        int64 pages = uvm_ustack_grow(proc->pgtbl, proc->ustack_npage, stval);
        if (pages < 0)
            user_fault_stop(proc, scause, sepc, stval);
        proc->ustack_npage = pages;
    }
    else
    {
        user_fault_stop(proc, scause, sepc, stval);
    }

    trap_user_return();
    panic("trap_user_handler: return path failed");
}

// 准备trapframe和S-mode CSR，再通过TRAMPOLINE中的user_return切换页表。
void trap_user_return(void)
{
    // 首次swtch可能继承main的SIE，必须先关闭，避免stvec切换过程出现窗口。
    intr_off();

    proc_t *proc = myproc();
    assert(proc != NULL && proc->pgtbl != NULL && proc->tf != NULL,
           "trap_user_return: invalid process");
    assert(proc->kstack == KSTACK(0),
           "trap_user_return: invalid kernel stack");

    uint64 user_vector_offset = (uint64)user_vector - (uint64)trampoline;
    uint64 user_return_offset = (uint64)user_return - (uint64)trampoline;
    assert(user_vector_offset < PGSIZE && user_return_offset < PGSIZE,
           "trap_user_return: trampoline overflow");

    proc->tf->user_to_kern_satp = r_satp();
    proc->tf->user_to_kern_sp = proc->kstack + PGSIZE;
    proc->tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    proc->tf->user_to_kern_hartid = (uint64)mycpuid();

    w_stvec(TRAMPOLINE + user_vector_offset);
    w_sepc(proc->tf->user_to_kern_epc);

    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP;
    sstatus |= SSTATUS_SPIE;
    w_sstatus(sstatus);

    void (*return_to_user)(uint64, uint64) =
        (void (*)(uint64, uint64))(TRAMPOLINE + user_return_offset);
    return_to_user(TRAPFRAME, MAKE_SATP(proc->pgtbl));
    panic("trap_user_return: unexpected return");
}

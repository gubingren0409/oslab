#pragma once

// proc.c: 页表初始化 + 第一个进程初始化

pgtbl_t proc_pgtbl_init(uint64 trapframe);
void proc_make_first(void);
void proc_init(void);
proc_t *proc_alloc(void);
void proc_free(proc_t *p);
void proc_return(void) __attribute__((noreturn));
int proc_fork(void);
void proc_yield(void);
void proc_sched(void);
void proc_scheduler(void) __attribute__((noreturn));
void proc_sleep(void *channel, spinlock_t *lock);
void proc_wakeup(void *channel);
void proc_exit(int status) __attribute__((noreturn));
int proc_wait(uint64 user_addr);

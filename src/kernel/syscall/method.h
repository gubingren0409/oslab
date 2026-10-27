#pragma once
void syscall(void);
uint64 arg_raw(int n);
void arg_uint32(int n, uint32 *value);
void arg_uint64(int n, uint64 *value);
int arg_str(int n, char *buf, int maxlen);
uint64 sys_helloworld(void);
uint64 sys_copyin(void);
uint64 sys_copyout(void);
uint64 sys_copyinstr(void);
uint64 sys_brk(void);
uint64 sys_mmap(void);
uint64 sys_munmap(void);
uint64 sys_printf(void);

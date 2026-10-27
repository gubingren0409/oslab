#include "sys.h"
#define PAGE 4096ul
#define TOP ((1ul << 38) - 2 * PAGE)
#define MMAP_END (TOP - 16ul * 256 * PAGE)
#define MMAP_BEGIN (MMAP_END - 64ul * 256 * PAGE)

static __attribute__((noreturn)) void stop(const char *message)
{
    syscall(SYS_printf, message);
    while (1)
        asm volatile("" ::: "memory");
}
#define CHECK(c) do { if (!(c)) stop("LAB5_FAIL\n"); } while (0)

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    // volatile 保证真实访问栈，编译优化不能删除缺页触发点。
    volatile char stack[PAGE * 4];
    stack[0] = 7;
    stack[PAGE * 4 - 1] = 9;
    CHECK(stack[0] == 7 && stack[PAGE * 4 - 1] == 9);
    int values[5];
    CHECK(syscall(SYS_copyout, values) == 5);
    CHECK(values[0] == 1 && values[4] == 5);
    CHECK(syscall(SYS_copyin, values, 5) == 0);
    CHECK(syscall(SYS_copyin, values, 1ul << 32) == -1);
    CHECK(syscall(SYS_copyout, 0) == -1);
    CHECK(syscall(0xfffffffffffffffful) == -1);
    long heap = syscall(SYS_brk, 0);
    CHECK(heap == 2 * PAGE);
    CHECK(syscall(SYS_brk, heap + 9 * PAGE + 17) == heap + 9 * PAGE + 17);
    volatile char *h = (char *)heap;
    h[8 * PAGE] = 42;
    CHECK(h[8 * PAGE] == 42);
    CHECK(syscall(SYS_brk, heap + 4 * PAGE) == heap + 4 * PAGE);
    CHECK(syscall(SYS_brk, 1) == -1);
    CHECK(syscall(SYS_brk, 0) == heap + 4 * PAGE);
    unsigned long address = mmap(0, 3 * PAGE);
    CHECK(address == MMAP_BEGIN);
    volatile char *m = (char *)address;
    m[0] = 11;
    m[2 * PAGE] = 33;
    CHECK(mmap((void *)address, PAGE) == 0);
    CHECK(mmap(0, (1ul << 32) + PAGE) == 0);
    CHECK(munmap((void *)(address + PAGE), PAGE) == 0);
    CHECK(m[0] == 11 && m[2 * PAGE] == 33);
    CHECK(munmap((void *)address, 3 * PAGE) == -1);
    CHECK(mmap((void *)(address + PAGE), PAGE) == address + PAGE);
    CHECK(munmap((void *)address, 3 * PAGE) == 0);
    CHECK(syscall(SYS_copyinstr, "LAB5_COPY_STRING_OK") == 0);
    stop("LAB5_PASS\n");
}

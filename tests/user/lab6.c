#include "sys.h"
#define PAGE 4096ul

static void fail(int line) __attribute__((noreturn));
static void fail(int line)
{
    syscall(SYS_print_str, "LAB6_FAIL line=");
    syscall(SYS_print_int, line);
    syscall(SYS_print_str, "\n");
    while (1)
        syscall(SYS_sleep, 100);
}
#define CHECK(c) do { if (!(c)) fail(__LINE__); } while (0)

__attribute__((section(".text.entry"), noreturn)) int main(void)
{
    volatile char stack[4 * PAGE];
    stack[0] = 71;
    stack[4 * PAGE - 1] = 72;
    CHECK(syscall(SYS_getpid) == 1);
    CHECK(syscall(SYS_wait, 0) == -1);
    CHECK(syscall(0xfffffffffffffffful) == -1);
    volatile char *heap = (char *)syscall(SYS_brk, 0);
    CHECK(syscall(SYS_brk, (long)heap + 2 * PAGE) == (long)heap + 2 * PAGE);
    volatile char *mapped = (char *)mmap(0, 2 * PAGE);
    CHECK(mapped != 0);
    heap[0] = 11;
    mapped[0] = 22;
    long child = syscall(SYS_fork);
    CHECK(child >= 0);
    if (child == 0)
    {
        CHECK(heap[0] == 11 && mapped[0] == 22 && stack[0] == 71);
        heap[0] = 33;
        mapped[0] = 44;
        stack[0] = 55;
        syscall(SYS_exit, 1234);
    }
    CHECK(syscall(SYS_wait, 1) == -1);
    int status = 0;
    CHECK(syscall(SYS_wait, &status) == child && status == 1234);
    CHECK(heap[0] == 11 && mapped[0] == 22 && stack[0] == 71);
    CHECK(syscall(SYS_wait, 0) == -1);
    CHECK(munmap((void *)mapped, 2 * PAGE) == 0);

    child = syscall(SYS_fork);
    CHECK(child >= 0);
    if (child == 0)
    {
        asm volatile("ld zero, 0(zero)" ::: "memory");
        fail(__LINE__);
    }
    CHECK(syscall(SYS_wait, &status) == child && status == -1);

    child = syscall(SYS_fork);
    CHECK(child >= 0);
    if (child == 0)
    {
        syscall(SYS_sleep, 30);
        syscall(SYS_print_str, "LAB6_SLEEP_WAKE\n");
        syscall(SYS_exit, 30);
    }
    long worker = syscall(SYS_fork);
    CHECK(worker >= 0);
    if (worker == 0)
    {
        syscall(SYS_print_str, "LAB6_RUNNABLE_WHILE_SLEEP\n");
        syscall(SYS_exit, 7);
    }
    long pid1 = syscall(SYS_wait, &status);
    CHECK(pid1 == worker && status == 7);
    CHECK(syscall(SYS_wait, &status) == child && status == 30);
    for (int i = 0; i < 8; i++)
    {
        child = syscall(SYS_fork);
        CHECK(child >= 0);
        if (child == 0)
            syscall(SYS_exit, i);
        CHECK(syscall(SYS_wait, &status) == child && status == i);
    }
    syscall(SYS_print_str, "LAB6_PASS\n");
    while (1)
        syscall(SYS_sleep, 100);
}

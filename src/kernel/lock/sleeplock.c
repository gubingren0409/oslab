#include "mod.h"
#include "../proc/mod.h"

void sleeplock_init(sleeplock_t *lk, char *name)
{
    spinlock_init(&lk->lock, "sleeplock");
    lk->locked = false;
    lk->pid = 0;
    lk->name = name;
}

bool sleeplock_holding(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    proc_t *p = myproc();
    bool owns = p && lk->locked && lk->pid == p->pid;
    spinlock_release(&lk->lock);
    return owns;
}

void sleeplock_acquire(sleeplock_t *lk)
{
    proc_t *p = myproc();
    assert(p != NULL, "sleeplock_acquire: no process");
    spinlock_acquire(&lk->lock);
    assert(!lk->locked || lk->pid != p->pid, "sleeplock_acquire: recursive");
    while (lk->locked)
        proc_sleep(lk, &lk->lock);
    lk->locked = true;
    lk->pid = p->pid;
    spinlock_release(&lk->lock);
}

void sleeplock_release(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    proc_t *p = myproc();
    assert(p && lk->locked && lk->pid == p->pid, "sleeplock_release: not owner");
    lk->locked = false;
    lk->pid = 0;
    proc_wakeup(lk);
    spinlock_release(&lk->lock);
}

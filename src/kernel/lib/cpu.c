#include "mod.h"

static cpu_t cpus[NCPU];

cpu_t *mycpu(void)
{
    int hartid = r_tp();
    assert(hartid >= 0 && hartid < NCPU, "mycpu: invalid hart id");
    return &cpus[hartid];
}

int mycpuid(void)
{
    int hartid = r_tp();
    assert(hartid >= 0 && hartid < NCPU, "mycpuid: invalid hart id");
    return hartid;
}

proc_t *myproc(void)
{
    push_off();
    proc_t *p = mycpu()->proc;
    pop_off();
    return p;
}

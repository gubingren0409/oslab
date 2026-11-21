#include "mod.h"
#include "../proc/mod.h"
#define INPUT_SIZE 128
static struct { spinlock_t lk; uint32 r,w,e; char buf[INPUT_SIZE]; } cons;
void cons_init(void) { spinlock_init(&cons.lk,"console"); }
static void erase(void)
{
    uart_putc_sync('\b'); uart_putc_sync(' '); uart_putc_sync('\b');
}
uint32 cons_write(uint32 len,uint64 src,bool user)
{
    char temp[64]; uint32 done=0;
    spinlock_acquire(&cons.lk);
    while (done<len) {
        uint32 cut=MIN(len-done,sizeof(temp));
        if (user) {
            if (uvm_copyin(myproc()->pgtbl,(uint64)temp,src+done,cut)<0) break;
        } else memmove(temp,(void *)(src+done),cut);
        for (uint32 i=0;i<cut;i++) uart_putc_sync(temp[i]);
        done+=cut;
    }
    spinlock_release(&cons.lk);
    return done || !len ? done : (uint32)-1;
}
uint32 cons_read(uint32 len,uint64 dst,bool user)
{
    uint32 done=0;
    spinlock_acquire(&cons.lk);
    while (done<len) {
        while (cons.r==cons.w) proc_sleep(&cons.r,&cons.lk);
        char c=cons.buf[cons.r%INPUT_SIZE];
        if (user) {
            if (uvm_copyout(myproc()->pgtbl,dst+done,(uint64)&c,1)<0) break;
        } else *(char *)(dst+done)=c;
        cons.r++; done++;
        if (c=='\n') break;
    }
    spinlock_release(&cons.lk);
    return done || !len ? done : (uint32)-1;
}
void cons_edit(int c)
{
    spinlock_acquire(&cons.lk);
    if (c==21) {
        while (cons.e!=cons.w && cons.buf[(cons.e-1)%INPUT_SIZE]!='\n') { cons.e--; erase(); }
    } else if (c==8 || c==127) {
        if (cons.e!=cons.w) { cons.e--; erase(); }
    } else if (c && cons.e-cons.r<INPUT_SIZE) {
        if (c=='\r') c='\n';
        uart_putc_sync(c); cons.buf[cons.e++%INPUT_SIZE]=c;
        if (c=='\n' || cons.e-cons.r==INPUT_SIZE) { cons.w=cons.e; proc_wakeup(&cons.r); }
    }
    spinlock_release(&cons.lk);
}

#include "mod.h"
#include "../fs/mod.h"
#define MAXARGS 32
typedef struct {
    uint8 ident[16];
    uint16 type,machine;
    uint32 version;
    uint64 entry,phoff,shoff;
    uint32 flags;
    uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
} elf_header_t;
typedef struct {
    uint32 type,flags;
    uint64 off,va,pa,filesz,memsz,align;
} elf_program_t;
_Static_assert(sizeof(elf_header_t)==64 && sizeof(elf_program_t)==56,"ELF64 ABI");

int proc_exec(char *path,char **argv)
{
    proc_t *p=myproc();
    inode_t *ip=path_to_inode(path);
    if (!ip) return -1;
    inode_lock(ip);
    pgtbl_t pt=NULL;
    trapframe_t *tf=NULL;
    elf_header_t eh;
    uint64 high=USER_BASE;
    int argc=0;
    if (ip->disk_info.type!=INODE_TYPE_DATA ||
        inode_read_data(ip,0,sizeof(eh),&eh,false)!=sizeof(eh) ||
        eh.ident[0]!=0x7f || eh.ident[1]!='E' || eh.ident[2]!='L' || eh.ident[3]!='F' ||
        eh.ident[4]!=2 || eh.ident[5]!=1 || eh.ident[6]!=1 ||
        eh.type!=2 || eh.machine!=243 || eh.version!=1 || eh.ehsize!=sizeof(eh) ||
        eh.phentsize!=sizeof(elf_program_t) || !eh.phnum || eh.phnum>32 ||
        eh.phoff>ip->disk_info.size ||
        eh.phoff+(uint64)eh.phnum*sizeof(elf_program_t)>ip->disk_info.size) goto fail;
    tf=pmem_alloc(false);
    if (!tf || !(pt=proc_pgtbl_init((uint64)tf))) goto fail;
    for (uint32 i=0;i<eh.phnum;i++) {
        elf_program_t ph;
        if (inode_read_data(ip,eh.phoff+i*sizeof(ph),sizeof(ph),&ph,false)!=sizeof(ph)) goto fail;
        if (ph.type!=1) continue;
        if (ph.filesz>ph.memsz || ph.off>ip->disk_info.size ||
            ph.filesz>ip->disk_info.size-ph.off || ph.va<USER_BASE || ph.va>=MMAP_BEGIN ||
            ph.memsz>MMAP_BEGIN-ph.va || ph.va%PGSIZE ||
            (ph.align>1 && ((ph.align&(ph.align-1)) || ph.va%ph.align!=ph.off%ph.align))) goto fail;
        if (!ph.memsz) continue;
        int perm=PTE_U;
        if (ph.flags&4) perm|=PTE_R;
        if (ph.flags&2) perm|=PTE_W;
        if (ph.flags&1) perm|=PTE_X;
        if (!(perm&(PTE_R|PTE_X)) || ((perm&PTE_W) && !(perm&PTE_R)) || ph.memsz>0xffffffffUL ||
            uvm_heap_grow(pt,ph.va,ph.memsz,perm)!=ph.va+ph.memsz) goto fail;
        for (uint64 done=0;done<ph.filesz;) {
            uint32 cut=MIN(ph.filesz-done,PGSIZE);
            pte_t *pte=vm_getpte(pt,ph.va+done,false);
            if (!pte || inode_read_data(ip,ph.off+done,cut,(void *)PTE_TO_PA(*pte),false)!=cut) goto fail;
            done+=cut;
        }
        if (ph.va+ph.memsz>high) high=ph.va+ph.memsz;
    }
    if (eh.entry<USER_BASE || eh.entry>=MMAP_BEGIN) goto fail;
    pte_t *entry=vm_getpte(pt,eh.entry,false);
    if (!entry || (*entry&(PTE_V|PTE_U|PTE_X))!=(PTE_V|PTE_U|PTE_X)) goto fail;
    if (vm_user_alloc(pt,USER_STACK_TOP-PGSIZE,USER_STACK_TOP,PTE_R|PTE_W|PTE_U)<0) goto fail;
    uint64 sp=USER_STACK_TOP, pointers[MAXARGS+1];
    for (;argv[argc];argc++) {
        if (argc==MAXARGS) goto fail;
        uint32 len=strlen(argv[argc])+1;
        if (len>128 || sp<USER_STACK_TOP-PGSIZE+ALIGN_UP(len,16)) goto fail;
        sp-=ALIGN_UP(len,16);
        if (uvm_copyout(pt,sp,(uint64)argv[argc],len)<0) goto fail;
        pointers[argc]=sp;
    }
    pointers[argc]=0;
    uint64 bytes=ALIGN_UP((argc+1)*sizeof(uint64),16);
    if (sp<USER_STACK_TOP-PGSIZE+bytes) goto fail;
    sp-=bytes;
    if (uvm_copyout(pt,sp,(uint64)pointers,(argc+1)*sizeof(uint64))<0) goto fail;
    inode_unlock(ip); inode_put(ip); ip=NULL;
    tf->user_to_kern_epc=eh.entry; tf->sp=sp; tf->a0=argc; tf->a1=sp;

    /* Publish only after the complete new image and argv exist. */
    pgtbl_t oldpt=p->pgtbl;
    trapframe_t *oldtf=p->tf;
    mmap_region_t *oldmap=p->mmap;
    p->pgtbl=pt; p->tf=tf; p->mmap=NULL; p->ustack_npage=1;
    p->heap_top=p->heap_base=MAX(PGROUNDUP(high),USER_HEAP_BASE);
    char *base=path;
    for (char *s=path;*s;s++) if (*s=='/') base=s+1;
    memset(p->name,0,sizeof(p->name));
    memmove(p->name,base,MIN(strlen(base),sizeof(p->name)-1));
    uvm_destroy_pgtbl(oldpt); pmem_free((uint64)oldtf,false);
    while (oldmap) { mmap_region_t *next=oldmap->next; mmap_region_free(oldmap); oldmap=next; }
    asm volatile("fence.i" ::: "memory");
    return argc;
fail:
    inode_unlock(ip); inode_put(ip);
    uvm_destroy_pgtbl(pt);
    if (tf) pmem_free((uint64)tf,false);
    return -1;
}

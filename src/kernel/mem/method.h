#pragma once

/* pmem.c: 物理内存管理逻辑 */

void pmem_init(void);
bool check_inkernel(uint64 p);
uint32 pmem_available(bool in_kernel);
void *pmem_alloc(bool in_kernel);
void pmem_free(uint64 page, bool in_kernel);

/* kvm.c: 内核态虚拟内存管理 + 页表通用函数 */

pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
int vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);
void vm_freewalk(pgtbl_t pgtbl);
void vm_print(pgtbl_t pgtbl);
void kvm_init(void);
void kvm_inithart(void);

// 多页用户分配与复制共用页表事务，失败也回收本次中间页。
int vm_user_alloc(pgtbl_t pgtbl, uint64 begin, uint64 end, int perm);
int vm_user_copy(pgtbl_t old, pgtbl_t new);
int uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
int uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
int uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen);
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur, uint32 len);
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur, uint32 len);
int64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_npage, uint64 fault);
void uvm_destroy_pgtbl(pgtbl_t pgtbl);
int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap, uint64 stack, mmap_region_t *mmap);
void mmap_init(void);
mmap_region_t *mmap_region_alloc(void);
void mmap_region_free(mmap_region_t *region);
uint64 uvm_mmap_find(mmap_region_t *head, uint64 len, mmap_region_t **last, mmap_region_t **next);
uint64 uvm_mmap(uint64 begin, uint32 npages, int perm);
int uvm_munmap(uint64 begin, uint32 npages);
void uvm_show_mmaplist(mmap_region_t *head);

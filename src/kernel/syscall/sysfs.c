#include "mod.h"
#include "../fs/mod.h"
extern super_block_t sb;
uint64 sys_alloc_block(void) { return fs_ready() ? bitmap_alloc_block() : BLOCK_NUM_UNUSED; }
uint64 sys_alloc_inode(void) { return fs_ready() ? bitmap_alloc_inode() : BLOCK_NUM_UNUSED; }
uint64 sys_free_block(void)
{
    uint64 b = arg_raw(0);
    if (!fs_ready() || b < sb.data_firstblock || b >= sb.total_blocks) return -1;
    bitmap_free_block(b); return 0;
}
uint64 sys_free_inode(void)
{
    if (!fs_ready() || arg_raw(0) >= sb.total_inodes) return -1;
    bitmap_free_inode(arg_raw(0)); return 0;
}
uint64 sys_show_bitmap(void)
{
    if (!fs_ready() || arg_raw(0) > 1) return -1;
    bitmap_print(arg_raw(0) == 0); return 0;
}
uint64 sys_get_block(void)
{
    return fs_ready() && arg_raw(0) < sb.total_blocks ? (uint64)buffer_get(arg_raw(0)) : 0;
}
uint64 sys_read_block(void)
{
    buffer_t *b = fs_ready() ? buffer_handle(arg_raw(0)) : NULL;
    return b ? uvm_copyout(myproc()->pgtbl, arg_raw(1), (uint64)b->data, BLOCK_SIZE) : -1;
}
uint64 sys_write_block(void)
{
    buffer_t *b = fs_ready() ? buffer_handle(arg_raw(0)) : NULL;
    if (!b) return -1;
    void *temp = pmem_alloc(true);
    if (!temp) return -1;
    int result = uvm_copyin(myproc()->pgtbl, (uint64)temp, arg_raw(1), BLOCK_SIZE);
    if (result == 0) { memmove(b->data, temp, BLOCK_SIZE); buffer_write(b); }
    pmem_free((uint64)temp, true);
    return result;
}
uint64 sys_put_block(void)
{
    buffer_t *b = fs_ready() ? buffer_handle(arg_raw(0)) : NULL;
    if (!b) return -1;
    buffer_put(b); return 0;
}
uint64 sys_show_buffer(void) { if (!fs_ready()) return -1; buffer_print_info(); return 0; }
uint64 sys_flush_buffer(void) { return fs_ready() ? buffer_freemem(arg_raw(0) > N_BUFFER ? N_BUFFER : arg_raw(0)) : 0; }

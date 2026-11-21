#include "mod.h"
extern super_block_t sb;
static uint32 alloc(uint32 first, uint32 count, uint32 base)
{
    for (uint32 start = 0; start < count; start += BIT_PER_BLOCK) {
        buffer_t *b = buffer_get(first + start / BIT_PER_BLOCK);
        if (!b) return BLOCK_NUM_UNUSED;
        uint32 valid = count - start;
        if (valid > BIT_PER_BLOCK) valid = BIT_PER_BLOCK;
        for (uint32 bit = 0; bit < valid; bit++) {
            uint8 mask = 1u << (bit % 8);
            if (!(b->data[bit / 8] & mask)) {
                b->data[bit / 8] |= mask;
                buffer_write(b);
                buffer_put(b);
                return base + start + bit;
            }
        }
        buffer_put(b);
    }
    return BLOCK_NUM_UNUSED;
}
static void clear(uint32 first, uint32 count, uint32 bit)
{
    if (bit >= count) return;
    buffer_t *b = buffer_get(first + bit / BIT_PER_BLOCK);
    if (!b) return;
    bit %= BIT_PER_BLOCK;
    b->data[bit / 8] &= ~(1u << (bit % 8));
    buffer_write(b);
    buffer_put(b);
}
uint32 bitmap_alloc_block(void) { return alloc(sb.data_bitmap_firstblock, sb.data_blocks, sb.data_firstblock); }
uint32 bitmap_alloc_inode(void) { return alloc(sb.inode_bitmap_firstblock, sb.total_inodes, 0); }
void bitmap_free_block(uint32 block) { if (block >= sb.data_firstblock) clear(sb.data_bitmap_firstblock, sb.data_blocks, block - sb.data_firstblock); }
void bitmap_free_inode(uint32 inode) { clear(sb.inode_bitmap_firstblock, sb.total_inodes, inode); }
void bitmap_print(bool data)
{
    uint32 count = data ? sb.data_blocks : sb.total_inodes;
    uint32 first = data ? sb.data_bitmap_firstblock : sb.inode_bitmap_firstblock;
    uint32 base = data ? sb.data_firstblock : 0;
    printf("%s bitmap allocated:", data ? "data" : "inode");
    for (uint32 start = 0; start < count; start += BIT_PER_BLOCK) {
        buffer_t *b = buffer_get(first + start / BIT_PER_BLOCK);
        if (!b) break;
        uint32 valid = count - start;
        if (valid > BIT_PER_BLOCK) valid = BIT_PER_BLOCK;
        for (uint32 i = 0; i < valid; i++)
            if (b->data[i / 8] & (1u << (i % 8))) printf(" %d", base + start + i);
        buffer_put(b);
    }
    printf("\n");
}

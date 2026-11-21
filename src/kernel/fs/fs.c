#include "mod.h"

super_block_t sb; /* 超级块 */
static bool ready;
bool fs_ready(void) { return ready; }

/* 基于superblock输出磁盘布局信息 (for debug) */
static void sb_print()
{
	printf("\ndisk layout information:\n");
	printf("1. super block:  block[0]\n");
	printf("2. inode bitmap: block[%d - %d]\n", sb.inode_bitmap_firstblock,
		sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1);
	printf("3. inode region: block[%d - %d]\n", sb.inode_firstblock,
		sb.inode_firstblock + sb.inode_blocks - 1);
	printf("4. data bitmap:  block[%d - %d]\n", sb.data_bitmap_firstblock,
		sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1);
	printf("5. data region:  block[%d - %d]\n", sb.data_firstblock,
		sb.data_firstblock + sb.data_blocks - 1);
	printf("block size = %d Byte, total size = %d MB, total inode = %d\n\n", sb.block_size,
		(int)((unsigned long long)(sb.total_blocks) * sb.block_size / 1024 / 1024), sb.total_inodes);
}

/* 文件系统初始化 */
void fs_init()
{
#if FS_ENABLED
#ifdef VISIONFIVE2
    if (!SDIO_FS_FIRST_SECTOR || !SDIO_FS_SECTORS) {
        printf("FS unavailable: configure a dedicated SDIO partition first\n");
        return;
    }
    sdio_disk_init();
#endif
    buffer_init();
    buffer_t *b = buffer_get(FS_SB_BLOCK);
    assert(b != NULL, "fs_init: superblock buffer");
    memmove(&sb, b->data, sizeof(sb));
    buffer_put(b);
    assert(sb.magic_num == FS_MAGIC && sb.block_size == BLOCK_SIZE &&
           sb.inode_bitmap_firstblock == 1 && sb.total_inodes && sb.data_blocks &&
           sb.inode_bitmap_blocks == (sb.total_inodes + BIT_PER_BLOCK - 1) / BIT_PER_BLOCK &&
           sb.inode_firstblock == 1 + sb.inode_bitmap_blocks &&
           sb.inode_blocks == (sb.total_inodes + INODE_PER_BLOCK - 1) / INODE_PER_BLOCK &&
           sb.data_bitmap_firstblock == sb.inode_firstblock + sb.inode_blocks &&
           sb.data_bitmap_blocks == (sb.data_blocks + BIT_PER_BLOCK - 1) / BIT_PER_BLOCK &&
           sb.data_firstblock == sb.data_bitmap_firstblock + sb.data_bitmap_blocks &&
           sb.total_blocks == sb.data_firstblock + sb.data_blocks,
           "fs_init: invalid filesystem geometry");
    ready = true;
    sb_print();
#else
    (void)sb_print;
#endif
}

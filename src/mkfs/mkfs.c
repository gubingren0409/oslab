#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include "mkfs.h"
static int disk_fd;
static super_block_t sb;
void block_write(unsigned int block, void *data)
{
    if (pwrite(disk_fd, data, BLOCK_SIZE, (off_t)block * BLOCK_SIZE) != BLOCK_SIZE) {
        perror("pwrite"); exit(1);
    }
}
void block_read(unsigned int block, void *data)
{
    if (pread(disk_fd, data, BLOCK_SIZE, (off_t)block * BLOCK_SIZE) != BLOCK_SIZE) {
        perror("pread"); exit(1);
    }
}
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s NEW_IMAGE [data_blocks [inodes]]\n", argv[0]); return 1;
    }
    const uint16_t endian = 1;
    if (*(const uint8_t *)&endian != 1 || sizeof(inode_disk_t) != 64) return 1;
    uint64_t data = argc > 2 ? strtoull(argv[2], NULL, 10) : N_DATA_BLOCK;
    uint64_t inodes = argc > 3 ? strtoull(argv[3], NULL, 10) : N_INODE;
    if (!data || data > (1UL << 26) || !inodes || inodes > (1UL << 20)) return 1;
    sb.magic_num = FS_MAGIC;
    sb.block_size = BLOCK_SIZE;
    sb.total_inodes = inodes;
    sb.inode_bitmap_firstblock = 1;
    sb.inode_bitmap_blocks = COUNT_BLOCKS(inodes, BIT_PER_BLOCK);
    sb.inode_firstblock = 1 + sb.inode_bitmap_blocks;
    sb.inode_blocks = COUNT_BLOCKS(inodes, INODE_PER_BLOCK);
    sb.data_bitmap_firstblock = sb.inode_firstblock + sb.inode_blocks;
    sb.data_bitmap_blocks = COUNT_BLOCKS(data, BIT_PER_BLOCK);
    sb.data_firstblock = sb.data_bitmap_firstblock + sb.data_bitmap_blocks;
    sb.data_blocks = data;
    sb.total_blocks = sb.data_firstblock + data;
    disk_fd = open(argv[1], O_RDWR | O_CREAT | O_EXCL, 0666);
    if (disk_fd < 0) { perror(argv[1]); return 1; }
    if (ftruncate(disk_fd, (off_t)sb.total_blocks * BLOCK_SIZE)) { perror("ftruncate"); return 1; }
    unsigned char buf[BLOCK_SIZE] = {0};
    memcpy(buf, &sb, sizeof(sb));
    block_write(0, buf);
    if (fsync(disk_fd) || close(disk_fd)) { perror("close/fsync"); return 1; }
    printf("created %s: %u blocks, %u inodes (sparse)\n", argv[1], sb.total_blocks, sb.total_inodes);
    return 0;
}

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include "mkfs.h"
static int disk_fd;
static super_block_t sb;
typedef struct { char name[60]; unsigned int inode_num; } dentry_t;
static unsigned int next_inode, next_block;
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
static unsigned int alloc_bit(unsigned int first, unsigned int bit)
{
    unsigned char buf[BLOCK_SIZE];
    block_read(first + bit / BIT_PER_BLOCK, buf);
    buf[bit % BIT_PER_BLOCK / 8] |= 1u << (bit % 8);
    block_write(first + bit / BIT_PER_BLOCK, buf);
    return bit;
}
static unsigned int alloc_block(void)
{
    if (next_block >= sb.data_blocks) { fprintf(stderr,"image full\n"); exit(1); }
    return sb.data_firstblock + alloc_bit(sb.data_bitmap_firstblock, next_block++);
}
static void write_inode(unsigned int number, inode_disk_t *ip)
{
    unsigned char buf[BLOCK_SIZE];
    unsigned int block=sb.inode_firstblock + number / INODE_PER_BLOCK;
    block_read(block,buf);
    memcpy(buf + number % INODE_PER_BLOCK * sizeof(*ip),ip,sizeof(*ip));
    block_write(block,buf);
}
static void append(inode_disk_t *ip, const void *src, unsigned int len)
{
    while (len) {
        unsigned int index=ip->size/BLOCK_SIZE, pos=ip->size%BLOCK_SIZE;
        if (index >= 10) { fprintf(stderr,"mkfs fixture too large\n"); exit(1); }
        if (!ip->index[index]) ip->index[index]=alloc_block();
        unsigned char buf[BLOCK_SIZE];
        block_read(ip->index[index],buf);
        unsigned int cut=BLOCK_SIZE-pos < len ? BLOCK_SIZE-pos : len;
        memcpy(buf+pos,src,cut); block_write(ip->index[index],buf);
        ip->size+=cut; src=(const char *)src+cut; len-=cut;
    }
}
static void populate(void)
{
    inode_disk_t root={.type=1,.major=1,.minor=1,.nlink=1};
    alloc_bit(sb.inode_bitmap_firstblock,next_inode++);
    dentry_t entries[4]={{.name=".",.inode_num=0},{.name="..",.inode_num=0},
                         {.name="ABCD.txt",.inode_num=1},{.name="abcd.txt",.inode_num=2}};
    append(&root,entries,sizeof(entries));
    write_inode(0,&root);
    for (unsigned int file=1;file<=2;file++) {
        alloc_bit(sb.inode_bitmap_firstblock,next_inode++);
        inode_disk_t ip={.type=0,.major=1,.minor=1,.nlink=1};
        char alphabet[26];
        for (int j=0;j<26;j++) alphabet[j]=(file==1?'A':'a')+j;
        for (int j=0;j<(file==1?200:500);j++) append(&ip,alphabet,sizeof(alphabet));
        write_inode(file,&ip);
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
    if (data < 8 || data > (1UL << 26) || inodes < 3 || inodes > (1UL << 20)) return 1;
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
    populate();
    if (fsync(disk_fd) || close(disk_fd)) { perror("close/fsync"); return 1; }
    printf("created %s: %u blocks, %u inodes (sparse)\n", argv[1], sb.total_blocks, sb.total_inodes);
    return 0;
}

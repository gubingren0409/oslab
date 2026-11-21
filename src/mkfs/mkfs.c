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
static unsigned int map_index(unsigned int *slot,unsigned int level,unsigned int number)
{
    if (!*slot) *slot=alloc_block();
    if (!level) return *slot;
    unsigned int entries[BLOCK_SIZE/sizeof(unsigned int)];
    block_read(*slot,entries);
    unsigned int span=level==2 ? 1024 : 1;
    unsigned int result=map_index(&entries[number/span],level-1,number%span);
    block_write(*slot,entries);
    return result;
}
static void append(inode_disk_t *ip, const void *src, unsigned int len)
{
    if ((uint64_t)ip->size+len>UINT32_MAX) { fprintf(stderr,"file size overflow\n"); exit(1); }
    while (len) {
        unsigned int index=ip->size/BLOCK_SIZE, pos=ip->size%BLOCK_SIZE;
        unsigned int block;
        if (index<10) block=map_index(&ip->index[index],0,0);
        else if (index<2058) block=map_index(&ip->index[10+(index-10)/1024],1,(index-10)%1024);
        else if (index<2058+1024*1024) block=map_index(&ip->index[12],2,index-2058);
        else { fprintf(stderr,"mkfs file too large\n"); exit(1); }
        unsigned char buf[BLOCK_SIZE];
        block_read(block,buf);
        unsigned int cut=BLOCK_SIZE-pos < len ? BLOCK_SIZE-pos : len;
        memcpy(buf+pos,src,cut); block_write(block,buf);
        ip->size+=cut; src=(const char *)src+cut; len-=cut;
    }
}
static void add_program(char *path)
{
    char *base=strrchr(path,'/'); base=base ? base+1 : path;
    size_t len=strlen(base);
    if (len>4 && !strcmp(base+len-4,".elf")) len-=4;
    if (!len || len>=60 || next_inode>=sb.total_inodes) { fprintf(stderr,"bad input name/inode limit\n"); exit(1); }
    unsigned int num=alloc_bit(sb.inode_bitmap_firstblock,next_inode++);
    inode_disk_t ip={.type=0,.major=1,.minor=1,.nlink=1},root;
    unsigned char buf[BLOCK_SIZE];
    block_read(sb.inode_firstblock,buf); memcpy(&root,buf,sizeof(root));
    dentry_t de={.inode_num=num}; memcpy(de.name,base,len);
    if (root.size+sizeof(de)>BLOCK_SIZE) { fprintf(stderr,"root full\n"); exit(1); }
    int fd=open(path,O_RDONLY);
    if (fd<0) { perror(path); exit(1); }
    ssize_t n;
    while ((n=read(fd,buf,sizeof(buf)))>0) append(&ip,buf,n);
    if (n<0 || close(fd)) { perror("read/close program"); exit(1); }
    write_inode(num,&ip); append(&root,&de,sizeof(de)); write_inode(0,&root);
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
    if (argc < 2) {
        fprintf(stderr, "usage: %s NEW_IMAGE [data_blocks [inodes [ELF ...]]]\n", argv[0]); return 1;
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
    for (int i=4;i<argc;i++) add_program(argv[i]);
    if (fsync(disk_fd) || close(disk_fd)) { perror("close/fsync"); return 1; }
    printf("created %s: %u blocks, %u inodes (sparse)\n", argv[1], sb.total_blocks, sb.total_inodes);
    return 0;
}

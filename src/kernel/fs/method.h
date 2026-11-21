#pragma once

/* virtio.c: 以block为单位的磁盘读写能力 */

void virtio_disk_init();
void virtio_disk_rw(buffer_t *b, bool write);
void virtio_disk_intr();

/* sdio.c: 以block为单位的SD卡读写能力 */

void sdio_disk_init();
void sdio_disk_rw(buffer_t *b, bool write);
void sdio_disk_intr();
// void sdio_disk_test();

/* buffer.c: 以buffer为中介沟通内存和磁盘 */

void buffer_init();
buffer_t* buffer_get(uint32 block_num);
void buffer_put(buffer_t *buf);
void buffer_write(buffer_t *buf);
uint32 buffer_freemem(uint32 buffer_count);
void buffer_print_info();

/* bitmap.c: data_bitmap和inode_bitmap的管理 */

uint32 bitmap_alloc_block();
uint32 bitmap_alloc_inode();
void bitmap_free_block(uint32 block_num);
void bitmap_free_inode(uint32 inode_num);
void bitmap_print(bool print_data_bitmap);

/* fs.c: 文件系统 */

void fs_init();
bool fs_ready(void);
buffer_t *buffer_handle(uint64 handle);
void buffer_release_owned(void);
void inode_init(void);
void inode_rw(inode_t *ip, bool write);
inode_t *inode_get(uint32 inode_num);
inode_t *inode_create(uint16 type, uint16 major, uint16 minor);
inode_t *inode_dup(inode_t *ip);
void inode_lock(inode_t *ip);
void inode_unlock(inode_t *ip);
void inode_put(inode_t *ip);
void inode_delete(inode_t *ip);
uint32 inode_read_data(inode_t *ip, uint32 offset, uint32 len, void *dst, bool user);
uint32 inode_write_data(inode_t *ip, uint32 offset, uint32 len, void *src, bool user);
void inode_print(inode_t *ip, char *name);
uint32 dentry_search(inode_t *ip, char *name);
uint32 dentry_create(inode_t *ip, uint32 num, char *name);
uint32 dentry_delete(inode_t *ip, char *name);
void dentry_print(inode_t *ip);
inode_t *path_to_inode(char *path);
inode_t *path_to_parent_inode(char *path, char *name);
void dentry_init(void);
uint32 dentry_search_2(inode_t *ip, uint32 num, char *name);
uint32 dentry_transmit(inode_t *ip, uint64 dst, uint32 len, bool user);
uint32 inode_to_path(inode_t *ip, char *path, uint32 len);
inode_t *path_create_inode(char *path, uint16 type, uint16 major, uint16 minor);
uint32 path_link(char *oldpath, char *newpath);
uint32 path_unlink(char *path);
void file_init(void);
file_t *file_alloc(void);
file_t *file_open(char *path, uint32 mode);
void file_close(file_t *f);
file_t *file_dup(file_t *f);
uint32 file_read(file_t *f, uint32 len, uint64 dst, bool user);
uint32 file_write(file_t *f, uint32 len, uint64 src, bool user);
uint32 file_lseek(file_t *f, uint32 offset, uint32 flag);
uint32 file_get_stat(file_t *f, uint64 dst);
void device_init(void);
bool device_open_check(uint16 major, uint32 mode);
uint32 device_read_data(uint16 major, uint32 len, uint64 dst, bool user);
uint32 device_write_data(uint16 major, uint32 len, uint64 src, bool user);

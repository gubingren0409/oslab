#pragma once
#include "../arch/type.h"
#include "../mem/type.h"

/*----------------------关于磁盘----------------------*/

#define VIRTIO_BASE 0x10001000
#define VIRTIO_IRQ 1

#define R(r) ((volatile uint32 *)(VIRTIO_BASE + (r)))

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1

#define VIRTIO_NUM 8

typedef struct vring_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
} vring_desc_t;

typedef struct vring_used_elem {
    uint32 id;
    uint32 len;
} vring_used_elem_t;

typedef struct used_area {
    uint16 flags;
    uint16 id;
    vring_used_elem_t elems[VIRTIO_NUM];
} used_area_t;

typedef struct disk {
    // 驱动需要8KB的连续空间, 不适合用pmem_alloc来申请
    // 所以直接定义在这里
    char pages[2 * PGSIZE];
    
    vring_desc_t *desc;
    used_area_t *used;
    uint16 *avail;
    char free[VIRTIO_NUM];
    uint16 used_idx;
    struct
    {
        struct buffer *b;
        char status;
    } info[VIRTIO_NUM];
    spinlock_t vdisk_lock;  
} disk_t;

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070

#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER 2
#define VIRTIO_CONFIG_S_DRIVER_OK 4
#define VIRTIO_CONFIG_S_FEATURES_OK 8

#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_SCSI 7  
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ 12
#define VIRTIO_F_ANY_LAYOUT 27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX 29

/*-------------------关于块缓冲区--------------------*/

#define BLOCK_SIZE 4096              // 基本管理单位的大小
#define SECTOR_SIZE 512              // SD卡的扇区大小
#define N_BUFFER_TEST 8              // 测试时的N_BUFFER取值
#ifndef N_BUFFER
#define N_BUFFER (32 * 512)          // 16384 cache slots, data pages allocated lazily
#endif
#ifndef SDIO_FS_FIRST_SECTOR
#define SDIO_FS_FIRST_SECTOR 0UL
#endif
#ifndef SDIO_FS_SECTORS
#define SDIO_FS_SECTORS 0UL
#endif
#define BLOCK_NUM_UNUSED 0xFFFFFFFF  // 未使用的Buffer需要将block_num设为这个值

/* 以Block为单位在内存和磁盘间传递数据 */
typedef struct buffer {
    /*
        锁的说明:
        1. block_num和ref由全局的自旋锁lk_buf_cache保护
        2. data和disk由内部的睡眠锁slk保护
    */
    uint32 block_num;                // buffer对应的磁盘内block序号 
    uint32 ref;                      // 引用数 (该buffer被get的次数)
    sleeplock_t slk;                 // 睡眠锁
    uint8* data;                     // block数据(大小为BLOCK_SIZE)
    bool disk;                       // 在virtio.c中使用
    bool valid;
} buffer_t;

/* 将buffer这种数据结构包装成资源节点 */
typedef struct buffer_node {
    buffer_t buf;                     // 资源
    struct buffer_node *next;         // 链接
    struct buffer_node *prev;         // 链接
} buffer_node_t;

/*-------------------关于文件系统--------------------*/

#define FS_MAGIC 0x12341234                 // 魔数
#define FS_SB_BLOCK 0                       // 超级块的序号

/* 超级块 */
typedef struct super_block {
    unsigned int magic_num;                  // 用于标识文件系统类型
    unsigned int block_size;                 // 基本存储单位的大小 (字节)
    unsigned int total_blocks;               // 总共的块数量
    unsigned int total_inodes;               // 总共的inode数量

    unsigned int inode_bitmap_firstblock;    // inode_bitmap区域的起始块号
	unsigned int inode_bitmap_blocks;        // inode_bitmap区域的块数量
	unsigned int inode_firstblock;           // inode区域的起始块号
    unsigned int inode_blocks;               // inode区域的块数量
	unsigned int data_bitmap_firstblock;     // data_bitmap区域的起始块号
	unsigned int data_bitmap_blocks;         // data_bitmap区域的块数量
	unsigned int data_firstblock;            // data区域的起始块号
    unsigned int data_blocks;                // data区域的块数量
} super_block_t;

/* 索引节点(64 Byte) */
typedef struct inode_disk {
    short type;                              // 文件类型
    short major;                             // 主设备号
    short minor;                             // 次设备号
    short nlink;                             // 链接数
    unsigned int size;                       // 文件数据长度(字节)
    unsigned int index[13];                  // 数据存储位置(10+2+1)
} inode_disk_t;

#define BIT_PER_BYTE 8
#define BIT_PER_BLOCK (BLOCK_SIZE * BIT_PER_BYTE)
#define INODE_PER_BLOCK (BLOCK_SIZE / sizeof(inode_disk_t))

#define INODE_TYPE_DATA 0
#define INODE_TYPE_DIR 1
#define INODE_TYPE_DIVICE 2
#define INODE_MAJOR_DEFAULT 1
#define INODE_MINOR_DEFAULT 1
#define INODE_INDEX_1 10
#define INODE_INDEX_2 12
#define INODE_INDEX_3 13
#define INODE_BLOCK_INDEX_1 10
#define INODE_BLOCK_INDEX_2 (10 + 2048)
#define INODE_BLOCK_INDEX_3 (10 + 2048 + 1024 * 1024)
#define INODE_MAX_SIZE 0xffffffffU
#define ROOT_INODE 0
#define N_INODE 64
#define MAXLEN_FILENAME 60
#define INVALID_INODE_NUM 0xffffffffU
typedef struct inode {
    inode_disk_t disk_info;
    bool valid_info;
    bool deleting;
    uint32 inode_num, ref;
    sleeplock_t slk;
} inode_t;
typedef struct dentry { char name[MAXLEN_FILENAME]; uint32 inode_num; } dentry_t;
#define DENTRY_PER_BLOCK (BLOCK_SIZE / sizeof(dentry_t))
#define COUNT_BLOCKS(n, per) (((n) + (per) - 1) / (per))


/*----------------------关于SD卡(SDIO)----------------------*/

/*
    以下内容参考Ailen实现的SD卡驱动, 有一些小的修改
    https://github.com/os-module/visionfive2-sd/tree/main
*/

// === 寄存器基地址与偏移 ===
#define SDIO_BASE               0x16020000UL

#define CTRL_REG                (SDIO_BASE + 0x00)
#define POWER_REG               (SDIO_BASE + 0x04)
#define CLK_DIVIDER_REG         (SDIO_BASE + 0x08)
#define CLOCK_ENABLE_REG        (SDIO_BASE + 0x10)
#define CTYPE_REG               (SDIO_BASE + 0x18)
#define BLK_SIZE_REG            (SDIO_BASE + 0x1C)
#define BYTE_CNT_REG            (SDIO_BASE + 0x20)
#define ARG_REG                 (SDIO_BASE + 0x28)
#define CMD_REG                 (SDIO_BASE + 0x2C)
#define RESP0_REG               (SDIO_BASE + 0x30)
#define RESP1_REG               (SDIO_BASE + 0x34)
#define RESP2_REG               (SDIO_BASE + 0x38)
#define RESP3_REG               (SDIO_BASE + 0x3C)
#define RAW_INT_STATUS_REG      (SDIO_BASE + 0x44)
#define STATUS_REG              (SDIO_BASE + 0x48)
#define CDETECT_REG             (SDIO_BASE + 0x50)
#define BUS_MODE_REG            (SDIO_BASE + 0x80)
#define DBADDRL_REG             (SDIO_BASE + 0x88)
#define DBADDRU_REG             (SDIO_BASE + 0x8C)
#define FIFO_DATA_REG           (SDIO_BASE + 0x600)

// === 内存映射 I/O 宏 ===
#define REG32(addr) (*(volatile uint32*)(addr))
#define REG64(addr) (*(volatile uint64*)(addr))

// === StatusReg ===
#define STATUS_DATA_BUSY(val)           (((val) >> 9) & 1U)
#define STATUS_FIFO_COUNT(val)          (((val) >> 17) & 0x1FFFU)

// === RawInterruptStatusReg ===
#define RAW_INT_INT_STATUS(val)         ((uint16)((val) & 0xFFFFU))

// === RawInterrupt (u16, MSB-first) ===
#define RAW_INTR_COMMAND_DONE(val)      (((val) >> 2) & 1U)
#define RAW_INTR_RXDR(val)              (((val) >> 5) & 1U)
#define RAW_INTR_TXDR(val)              (((val) >> 4) & 1U)
#define RAW_INTR_DTO(val)               (((val) >> 3) & 1U)
#define RAW_INTR_RTO(val)               (((val) >> 8) & 1U)
#define RAW_INTR_DCRC(val)              (((val) >> 7) & 1U)
#define RAW_INTR_RESPONSE_ERR(val)      (((val) >> 1) & 1U)
#define RAW_INTR_DRTO(val)              (((val) >> 9) & 1U)
#define RAW_INTR_SBE(val)               (((val) >> 13) & 1U)
#define RAW_INTR_EBE(val)               (((val) >> 15) & 1U)
static inline bool raw_interrupt_have_error(uint16 intr) {
    return RAW_INTR_RTO(intr) || RAW_INTR_DCRC(intr) ||
           RAW_INTR_RESPONSE_ERR(intr) || RAW_INTR_DRTO(intr) ||
           RAW_INTR_SBE(intr) || RAW_INTR_EBE(intr);
}

// === 中断相关 ===

#define SDIO_IRQ 75 // JH7110的PLIC中断号: mmc1 (SDIO_BASE = 0x16020000)

#define INT_MASK_REG            (SDIO_BASE + 0x24)

// CTRL_REG中的中断总开关
#define SDIO_CTRL_INT_ENABLE    (1U << 4)

// RawInterruptStatusReg / INT_MASK_REG 的位掩码
#define RAW_INTR_DTO_MASK       (1U << 3)
#define RAW_INTR_TXDR_MASK      (1U << 4)
#define RAW_INTR_RXDR_MASK      (1U << 5)
#define RAW_INTR_ERROR_MASK     ((1U << 1) |  /* RE   */ \
                                 (1U << 7) |  /* DCRC */ \
                                 (1U << 8) |  /* RTO  */ \
                                 (1U << 9) |  /* DRTO */ \
                                 (1U << 13) | /* SBE  */ \
                                 (1U << 15))  /* EBE  */

// 数据传输期间需要打开的中断: 数据请求 + 传输结束 + 错误
#define SDIO_READ_INT_MASK      (RAW_INTR_DTO_MASK | RAW_INTR_RXDR_MASK | RAW_INTR_ERROR_MASK)
#define SDIO_WRITE_INT_MASK     (RAW_INTR_DTO_MASK | RAW_INTR_TXDR_MASK | RAW_INTR_ERROR_MASK)
// === CmdReg 构建 ===
typedef uint32 CmdReg;

// pub struct CmdReg {
//     pub start_cmd: bool,                // bit 31
//     reserved: bool,                     // bit 30
//     pub use_hold_reg: bool,             // bit 29
//     pub volt_switch: bool,              // bit 28
//     pub boot_mode: bool,                // bit 27
//     pub disable_boot: bool,             // bit 26
//     pub expect_boot_ack: bool,          // bit 25
//     pub enable_boot: bool,              // bit 24
//     pub ccs_expected: bool,             // bit 23
//     pub read_ceata_device: bool,        // bit 22
//     pub update_clock_registers_only: bool, // bit 21
//     #[bits(5)]
//     pub card_number: u16,               // bits 20–16
//     pub send_initialization: bool,      // bit 15
//     pub stop_abort_cmd: bool,           // bit 14
//     pub wait_prvdata_complete: bool,    // bit 13
//     pub send_auto_stop: bool,           // bit 12
//     pub transfer_mode: bool,            // bit 11
//     pub transfer_dir: bool,             // bit 10
//     pub data_expected: bool,            // bit 9
//     pub check_response_crc: bool,       // bit 8
//     pub response_length: bool,          // bit 7
//     pub response_expect: bool,          // bit 6
//     #[bits(6)]
//     pub cmd_index: u16,                 // bits 5–0
// }

// impl CmdReg {
//     pub fn default(card_number: usize, cmd_number: u8) -> Self {
//         CmdReg::new()
//             .with_start_cmd(true)
//             .with_use_hold_reg(true)
//             .with_response_expect(true)
//             .with_wait_prvdata_complete(true)
//             .with_check_response_crc(true)
//             .with_card_number(card_number as u16)
//             .with_cmd_index(cmd_number as u16)
//     }
//     pub fn with_no_data(card_number: usize, cmd_number: u8) -> Self {
//         CmdReg::default(card_number, cmd_number)
//     }

//     pub fn with_data(card_number: usize, cmd_number: u8) -> Self {
//         CmdReg::default(card_number, cmd_number).with_data_expected(true)
//     }
// }

static inline CmdReg cmd_reg_default(unsigned int card_number, uint8 cmd_index)
{
    CmdReg reg = 0;
    reg |= (1U << 31);                          // start_cmd
    reg |= (1U << 29);                          // use_hold_reg
    reg |= (1U << 13);                          // wait_prvdata_complete
    reg |= (1U << 8);                           // check_response_crc
    reg |= (1U << 6);                           // response_expect
    reg |= ((card_number & 0x1FU) << 16);       // card_number [20:16]
    reg |= (cmd_index & 0x3FU);                 // cmd_index [5:0]
    return reg;
}

static inline CmdReg cmd_reg_with_no_data(unsigned int card_number, uint8 cmd_index)
{
    return cmd_reg_default(card_number, cmd_index);
}

static inline CmdReg cmd_reg_with_data(unsigned int card_number, uint8 cmd_index)
{
    CmdReg reg = cmd_reg_default(card_number, cmd_index);
    reg |= (1U << 9);  // set bit 9 (data_expected)
    return reg;
}

// === 命令枚举 ===
typedef enum {
    CMD_GO_IDLE_STATE = 0,
    CMD_ALL_SEND_CID = 2,
    CMD_SEND_RELATIVE_ADDR = 3,
    CMD_SELECT_CARD = 7,
    CMD_SEND_IF_COND = 8,
    CMD_APP_CMD = 55,
    CMD_SEND_CSD = 9,
    CMD_SD_SEND_OP_COND = 41,
    CMD_SEND_SCR = 51,
    CMD_READ_SINGLE_BLOCK = 17,
    CMD_WRITE_SINGLE_BLOCK = 24,
    CMD_RESET_CLOCK = 111, // private 便于调试
} Cmd;

// impl From<Cmd> for CmdReg {
//     fn from(value: Cmd) -> Self {
//         match value {
//             Cmd::GoIdleState => {
//                 CmdReg::with_no_data(0, value.into()).with_send_initialization(true)
//             }
//             Cmd::SendIfCond | Cmd::AppCmd | Cmd::SendRelativeAddr | Cmd::SelectCard => {
//                 CmdReg::with_no_data(0, value.into())
//             }
//             Cmd::SdSendOpCond => {
//                 CmdReg::with_no_data(0, value.into()).with_check_response_crc(false)
//             }
//             Cmd::SendCsd => CmdReg::with_no_data(0, value.into()).with_check_response_crc(false),
//             Cmd::AllSendCid => CmdReg::with_no_data(0, value.into())
//                 .with_check_response_crc(false)
//                 .with_response_length(true),
//             Cmd::SendScr | Cmd::ReadSingleBlock => CmdReg::with_data(0, value.into()),
//             Cmd::WriteSingleBlock => CmdReg::with_data(0, value.into()).with_transfer_dir(true),
//             _ => {
//                 panic!("Not implemented")
//             }
//         }
//     }
// }

// 根据命令类型构建 CmdReg（与 Rust From<Cmd> for CmdReg 一致）
static inline CmdReg cmd_reg_from_cmd(Cmd cmd)
{
    switch (cmd)
    {
        case CMD_GO_IDLE_STATE:
            // set bit 15 (send initialization)
            return ((cmd_reg_with_no_data(0, cmd) | (1U << 15)));
 
        case CMD_ALL_SEND_CID:
            // set bit 7 (response length) + clear bit 8 (check response crc)
            return ((cmd_reg_with_no_data(0, cmd) | (1U << 7)) & ~(1U << 8));

        case CMD_SEND_RELATIVE_ADDR:
        case CMD_SELECT_CARD:
        case CMD_SEND_IF_COND:
        case CMD_APP_CMD:
            return (cmd_reg_with_no_data(0, cmd));

        case CMD_SEND_CSD:
        case CMD_SD_SEND_OP_COND:
            // clear bit 8 (check response crc)
            return (cmd_reg_with_no_data(0, cmd) & ~(1U << 8));

        case CMD_SEND_SCR:
        case CMD_READ_SINGLE_BLOCK:
            return cmd_reg_with_data(0, cmd);

        case CMD_WRITE_SINGLE_BLOCK:
            // set bit 10 (transfer_dir)
            return cmd_reg_with_data(0, cmd) | (1U << 10);

        default:
            return 0;
    }
}

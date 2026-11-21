/*
    以下内容参考Ailen实现的SD卡驱动, 有一些小的修改
    https://github.com/os-module/visionfive2-sd/tree/main

    数据读写和virtio.c采用相同的方式:
    1. sdio_disk_rw()只负责发起一次扇区传输, 然后睡眠等待
    2. sdio_disk_intr()在中断中搬运FIFO数据, 传输完成后唤醒等待的进程
    初始化阶段的命令没有数据, 仍然采用忙等方式 (不需要进程上下文)
*/

#include "mod.h"
#include "../trap/mod.h"
#include "../proc/mod.h"

// 状态检查

static bool can_send_cmd_condition(void)
{
    uint32 cmd_reg = REG32(CMD_REG);
    return !(cmd_reg & (1U << 31)); // start_cmd cleared
}

static bool response_done_condition(void)
{
    uint32 raw_int_status_reg = REG32(RAW_INT_STATUS_REG);
    uint16 int_status = RAW_INT_INT_STATUS(raw_int_status_reg);
    return RAW_INTR_COMMAND_DONE(int_status);
}

// 带超时检测的状态检查

static bool wait_util_can_send_cmd(void)
{
    uint64 start = r_time();
    while (!can_send_cmd_condition()) {
        if (r_time() - start >= INTERVAL) {
            printf("wait_util_can_send_cmd: timeout!\n");
            return false;
        }
    }
    return true;
}

static bool wait_util_response(void)
{
    uint64 start = r_time();
    while (!response_done_condition()) {
        if (r_time() - start >= INTERVAL) {
            printf("wait_util_response: timeout!\n");
            return false;
        }
    }
    return true;
}

static uint32 fifo_filled_cnt(void)
{
    uint32 status_reg = REG32(STATUS_REG);
    return STATUS_FIFO_COUNT(status_reg);
}

// 控制器同一时刻只能处理一个扇区传输
// 这个结构体记录正在进行中的请求, 所有字段由sdio.lk保护
typedef struct sdio_disk {
    spinlock_t lk;
    uint8 *data;     // 当前扇区的内存缓冲区 (NULL表示没有请求)
    bool write;      // true: 写入SD卡, false: 从SD卡读取
    bool busy;       // 控制器是否已被某个请求占用
    bool done;       // 当前扇区是否已经传输结束
    bool error;      // 当前扇区是否出错
    uint32 offset;   // 当前扇区已经搬运的字节数
    uint32 raw_int;  // 出错时的RAW_INT_STATUS, 用于诊断
} sdio_disk_t;

static sdio_disk_t sdio;

// 核心: 命令发送函数 (只用于初始化阶段的命令, 不传输数据)

static bool send_cmd(Cmd cmd_type, CmdReg cmd, uint32 arg, uint32 resp[4])
{
    uint32 raw_int_status;
    uint16 intr;

    // 等待上一条命令处理完毕
    if (!wait_util_can_send_cmd())
        return false;

    // 设置ARG寄存器和CMD寄存器
    REG32(ARG_REG) = arg;
    REG32(CMD_REG) = cmd;

    // 等待控制器收到命令
    if (!wait_util_can_send_cmd())
        return false;

    // 如果是需要回复的命令, 等待控制器回应
    if (cmd & (1U << 6))
        if (!wait_util_response())
            return false;

    // Clear interrupts
    raw_int_status = REG32(RAW_INT_STATUS_REG);
    REG32(RAW_INT_STATUS_REG) = raw_int_status;

    intr = RAW_INT_INT_STATUS(raw_int_status);
    if (raw_interrupt_have_error(intr))
        goto error;

    if (resp) {
        resp[0] = REG32(RESP0_REG);
        resp[1] = REG32(RESP1_REG);
        resp[2] = REG32(RESP2_REG);
        resp[3] = REG32(RESP3_REG);
    }

    return true;

error:
    // 特殊情况处理 (CMD0)
    if (cmd_type == CMD_GO_IDLE_STATE && RAW_INTR_RTO(intr) == 1)
        return true;

    // 输出错误信息
    printf("send_cmd: ERROR CmdTpye = %d, RTO = %d, RE = %d, DRTO = %d, DCRC = %d, SBE = %d, EBE = %d\n",
        cmd_type,
        RAW_INTR_RTO(intr),
        RAW_INTR_RESPONSE_ERR(intr),
        RAW_INTR_DRTO(intr),
        RAW_INTR_DCRC(intr),
        RAW_INTR_SBE(intr),
        RAW_INTR_EBE(intr));

    return false;
}

// 初始化辅助函数

static void set_transaction_size(uint32 blk_size, uint32 byte_count)
{
    REG32(BLK_SIZE_REG) = blk_size;
    REG32(BYTE_CNT_REG) = byte_count;
}

static void reset_clock(void)
{
    // disable clock
    REG32(CLOCK_ENABLE_REG) = 0x0;

    // start_cmd + wait_prvdata_complete + update_clock_registers_only
    CmdReg clock_cmd = (1U << 31) | (1U << 13) | (1U << 21);

    // Reset clock
    send_cmd(111, clock_cmd, 0, NULL);

    // 将时钟周期设为400kHz (low): 3.2MHz / (2 * 4) = 400kHz
    REG32(CLK_DIVIDER_REG) = 0x4;

    // enable clock
    REG32(CLOCK_ENABLE_REG) = 0x1;

    // Reset clock
    send_cmd(111, clock_cmd, 0, NULL);
}

static void reset_fifo(void)
{
    uint32 ctrl_reg = REG32(CTRL_REG);
    ctrl_reg |= (1U << 1); // 启动FIFO模式
    // REG32(FIFO_DATA_REG) = ctrl_reg; // 原版的奇怪操作 (运行不报错)
    REG32(CTRL_REG) = ctrl_reg; // 我觉得合理的写法 (运行也不报错)
}

static void reset_dma(void)
{
    uint32 bus_mode_reg = REG32(BUS_MODE_REG);
    bus_mode_reg |= (1U << 0); // set swr bit
    bus_mode_reg &= ~(1U << 7); // clear de bit
    REG32(BUS_MODE_REG) = bus_mode_reg;

    uint32 ctrl_reg = REG32(CTRL_REG);
    ctrl_reg |= (1U << 2);  // set dma_reset bit
    ctrl_reg &= ~(1U << 25); // clear use_internal_dmac bit
    REG32(CTRL_REG) = ctrl_reg;
}

static void check_version()
{
    uint32 resp[4];
    CmdReg cmd8 = cmd_reg_from_cmd(CMD_SEND_IF_COND);
    if (!send_cmd(CMD_SEND_IF_COND, cmd8, 0x1AA, resp))
        panic("CMD8 fial!");

    if ((resp[0] & 0xAA) == 0) {
        printf("CMD8 echo mismatch: %x\n", resp[0] & 0xFFF);
        panic("Card version: 1.0");
    }
}

static void wait_card_ready(void)
{
    while (1)
    {
        CmdReg cmd55 = cmd_reg_from_cmd(CMD_APP_CMD);
        send_cmd(CMD_APP_CMD, cmd55, 0, NULL);

        CmdReg cmd41 = cmd_reg_from_cmd(CMD_SD_SEND_OP_COND);
        uint32 cmd41_arg = (1U << 30) | (1U << 24) | 0xFF8000;
        uint32 resp[4];
        if (!send_cmd(CMD_SD_SEND_OP_COND, cmd41, cmd41_arg, resp))
            panic("cmd41 fail!");

        if (resp[0] & (1U << 31))
            break;

        uint64 start = r_time();
        while (r_time() - start < INTERVAL * 5);
    }
}

static void check_cid(void)
{
    CmdReg cmd2 = cmd_reg_from_cmd(CMD_ALL_SEND_CID);
    uint32 resp[4];
    send_cmd(CMD_ALL_SEND_CID, cmd2, 0, resp);
}

static uint32 get_rca(void)
{
    CmdReg cmd3 = cmd_reg_from_cmd(CMD_SEND_RELATIVE_ADDR);
    uint32 resp[4];
    if (!send_cmd(CMD_SEND_RELATIVE_ADDR, cmd3, 0, resp)) {
        printf("rca = %d\n", resp[0] >> 16);
        panic("get_rca: CMD3 failed");
    }
    return resp[0] >> 16;
}

static void check_csd(uint32 rca)
{
    CmdReg cmd9 = cmd_reg_from_cmd(CMD_SEND_CSD);
    uint32 resp[4];
    if (!send_cmd(CMD_SEND_CSD, cmd9, rca << 16, resp))
        panic("check_csd: CMD9 fail!");
}

static void select_card(uint32 rca)
{
    CmdReg cmd7 = cmd_reg_from_cmd(CMD_SELECT_CARD);
    if (!send_cmd(CMD_SELECT_CARD, cmd7, rca << 16, NULL))
        panic("select_card: CMD7 failed");
}

// static void print_reg_info(void)
// {
//     uint32 reg;

//     reg = REG32(CDETECT_REG);
//     printf("detect = %x\n", reg);

//     reg = REG32(POWER_REG);
//     printf("power = %x\n", reg);

//     reg = REG32(CLOCK_ENABLE_REG);
//     printf("clock_enabel = %x\n", reg);

//     reg = REG32(CTYPE_REG);
//     printf("card_type = %x\n", reg);

//     reg = REG32(CTRL_REG);
//     printf("ctrl = %x\n", reg);

//     reg = REG32(BUS_MODE_REG);
//     printf("bus_mode = %x\n", reg);

//     uint32 dma_desc_base_lower = REG32(DBADDRL_REG);
//     uint32 dma_desc_base_upper = REG32(DBADDRU_REG);
//     uint64 dma_desc_base = ((uint64)dma_desc_base_upper << 32) | dma_desc_base_lower;
//     printf("dma_desc_base = %p\n", dma_desc_base);

//     reg = REG32(CLK_DIVIDER_REG);
//     printf("clock_divider = %p\n", reg);
// }

// === 主初始化流程 ===
void sdio_disk_init(void)
{
    spinlock_init(&sdio.lk, "sdio_disk");

    reset_clock();

    reset_fifo();

    REG32(CTYPE_REG) = 0x0; // 启动1bit模式

    reset_dma();

    CmdReg cmd0 = cmd_reg_from_cmd(CMD_GO_IDLE_STATE);
    send_cmd(CMD_GO_IDLE_STATE, cmd0, 0, NULL);

    check_version();

    wait_card_ready();

    check_cid();

    uint32 rca = get_rca();
    // printf("rca = %d\n", rca);

    check_csd(rca);

    // 短暂时延
    uint32 start_time = r_time();
    while (r_time() - start_time < INTERVAL / 10);

    select_card(rca);

    // print_reg_info();

    // 清掉初始化阶段产生的中断状态
    uint32 raw_int_status = REG32(RAW_INT_STATUS_REG);
    REG32(RAW_INT_STATUS_REG) = raw_int_status;

    // 初始化期间不响应任何中断, 数据传输前才按方向打开
    REG32(INT_MASK_REG) = 0;

    // 打开控制器的中断输出
    REG32(CTRL_REG) |= SDIO_CTRL_INT_ENABLE;

    // printf("sdio_disk_init: success!\n");
}

// 从FIFO中取出一个64bit, 按字节写入data
static void fifo_pop(uint8 *data, uint32 *offset)
{
    uint64 fifo_data = REG64(FIFO_DATA_REG);
    for (int i = 0; i < 8 && *offset < SECTOR_SIZE; i++)
        data[(*offset)++] = (uint8)(fifo_data >> (i * 8));
}

// 从data中取出一个64bit, 写入FIFO
static void fifo_push(const uint8 *data, uint32 *offset)
{
    uint64 fifo_data = 0;
    for (int i = 0; i < 8 && *offset < SECTOR_SIZE; i++)
        fifo_data |= ((uint64)data[(*offset)++]) << (i * 8);
    REG64(FIFO_DATA_REG) = fifo_data;
}

// SD卡中断处理: 搬运FIFO数据, 完成扇区传输后唤醒进程
void sdio_disk_intr(void)
{
    spinlock_acquire(&sdio.lk);

    uint32 raw_int_status = REG32(RAW_INT_STATUS_REG);
    REG32(RAW_INT_STATUS_REG) = raw_int_status; // 写1清除
    uint16 intr = RAW_INT_INT_STATUS(raw_int_status);

    // 没有正在处理的请求 (例如初始化阶段的残留中断)
    if (sdio.data == NULL || sdio.done)
        goto out;

    if (raw_interrupt_have_error(intr)) {
        sdio.raw_int = raw_int_status;
        sdio.error = true;
        goto finish;
    }

    if (sdio.write) {
        // TXDR: FIFO有空位, 继续填充; DTO: 整个扇区已经发完
        while (fifo_filled_cnt() < 120 && sdio.offset < SECTOR_SIZE)
            fifo_push(sdio.data, &sdio.offset);

        if (sdio.offset < SECTOR_SIZE)
            goto out; // 等下一次TXDR

        if (!RAW_INTR_DTO(intr)) {
            // 数据已经全部写入FIFO, 只需要再等DTO
            REG32(INT_MASK_REG) &= ~RAW_INTR_TXDR_MASK;
            goto out;
        }
    } else {
        // RXDR: FIFO有数据, 持续读出; DTO: 整个扇区已经收完
        while (fifo_filled_cnt() >= 2 && sdio.offset < SECTOR_SIZE)
            fifo_pop(sdio.data, &sdio.offset);

        if (!RAW_INTR_DTO(intr))
            goto out; // 等下一次RXDR

        // DTO之后FIFO里可能还剩最后几个字节
        while (fifo_filled_cnt() > 0 && sdio.offset < SECTOR_SIZE)
            fifo_pop(sdio.data, &sdio.offset);

        if (sdio.offset < SECTOR_SIZE) {
            sdio.raw_int = raw_int_status;
            sdio.error = true;
            goto finish;
        }
    }

finish:
    // 当前扇区结束 (成功或失败), 唤醒等待的进程
    sdio.done = true;
    proc_wakeup(&sdio.done);

out:
    spinlock_release(&sdio.lk);
}

/* 发起一次扇区传输, 然后睡眠等待sdio_disk_intr()完成它 */
static bool transfer_sector(uint32 sector, uint8 *data, bool write)
{
    // 连续写入时需要一个短暂的时延, 否则会发生RTO超时错误
    uint64 start = r_time();
    while (r_time() - start < INTERVAL / 10);

    spinlock_acquire(&sdio.lk);

    // 控制器同一时刻只处理一个请求
    while (sdio.busy)
        proc_sleep(&sdio.busy, &sdio.lk);
    sdio.busy = true;

    if (!wait_util_can_send_cmd()) {
        sdio.busy = false;
        proc_wakeup(&sdio.busy);
        spinlock_release(&sdio.lk);
        return false;
    }

    // 清除上一次传输残留的中断状态
    uint32 raw_int_status = REG32(RAW_INT_STATUS_REG);
    REG32(RAW_INT_STATUS_REG) = raw_int_status;

    sdio.data = data;
    sdio.write = write;
    sdio.done = false;
    sdio.error = false;
    sdio.offset = 0;

    set_transaction_size(SECTOR_SIZE, SECTOR_SIZE);
    REG32(ARG_REG) = sector;
    REG32(INT_MASK_REG) = write ? SDIO_WRITE_INT_MASK : SDIO_READ_INT_MASK;
    REG32(CMD_REG) = write ? cmd_reg_from_cmd(CMD_WRITE_SINGLE_BLOCK)
                           : cmd_reg_from_cmd(CMD_READ_SINGLE_BLOCK);

    while (!sdio.done)
        proc_sleep(&sdio.done, &sdio.lk);

    // 关闭数据中断, 防止控制器空闲时反复触发
    REG32(INT_MASK_REG) = 0;

    bool error = sdio.error;
    uint32 raw_int = sdio.raw_int;
    sdio.data = NULL;
    sdio.busy = false;
    proc_wakeup(&sdio.busy);

    spinlock_release(&sdio.lk);

    if (error) {
        printf("sdio_disk_rw: %s sector %d failed, RTO = %d, RE = %d, DRTO = %d, DCRC = %d, SBE = %d, EBE = %d\n",
               write ? "write" : "read", sector,
               RAW_INTR_RTO(raw_int),
               RAW_INTR_RESPONSE_ERR(raw_int),
               RAW_INTR_DRTO(raw_int),
               RAW_INTR_DCRC(raw_int),
               RAW_INTR_SBE(raw_int),
               RAW_INTR_EBE(raw_int));
    }

    return !error;
}

static bool read_sector(uint32 sector, uint8 *buf)
{
    return transfer_sector(sector, buf, false);
}

static bool write_sector(uint32 sector, uint8 *buf)
{
    return transfer_sector(sector, buf, true);
}

// 块设备接口
void sdio_disk_rw(buffer_t *b, bool write)
{
    uint32 sec_per_block = BLOCK_SIZE / SECTOR_SIZE;
    uint64 first_sec = (uint64)b->block_num * sec_per_block;
    assert(SDIO_FS_FIRST_SECTOR && SDIO_FS_SECTORS &&
           first_sec + sec_per_block <= SDIO_FS_SECTORS &&
           first_sec + SDIO_FS_FIRST_SECTOR + sec_per_block <= 0x100000000UL,
           "SDIO filesystem partition not configured/out of bounds");
    first_sec += SDIO_FS_FIRST_SECTOR;
    bool ret;

    for (uint32 i = 0; i < sec_per_block; i++) {
        if (write)
            ret = write_sector((uint32)(first_sec + i), b->data + i * SECTOR_SIZE);
        else
            ret = read_sector((uint32)(first_sec + i), b->data + i * SECTOR_SIZE);

        if (!ret) {
            printf("sdio_disk_rw: %s sector %x failed!\n", write ? "write" : "read", first_sec + i);
            panic("sdio_disk_rw: fail!");
        }
    }
}

// Raw-sector destructive test intentionally omitted: all I/O uses configured partition bounds.

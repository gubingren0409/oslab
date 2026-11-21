#include "mod.h"
#include "../fs/mod.h"

// PLIC初始化
void plic_init()
{
    // 设置UART中断优先级
    *(volatile uint32 *)(PLIC_PRIORITY(UART_IRQ)) = 1;
#if FS_ENABLED
#ifdef VISIONFIVE2
    *(volatile uint32 *)(PLIC_PRIORITY(SDIO_IRQ)) = 1;
#else
    *(volatile uint32 *)(PLIC_PRIORITY(VIRTIO_IRQ)) = 1;
#endif
#endif
}

// PLIC核心初始化
void plic_inithart()
{
    int hartid = mycpuid();
    // 使能中断开关
    volatile uint32 *enable = (volatile uint32 *)PLIC_SENABLE(hartid);
#ifdef VISIONFIVE2
    for (int word = 0; word < 5; word++)
        enable[word] = 0;
#endif
    enable[UART_IRQ / 32] = 1u << (UART_IRQ % 32);
#if FS_ENABLED
#ifdef VISIONFIVE2
    if (SDIO_FS_FIRST_SECTOR && SDIO_FS_SECTORS)
        enable[SDIO_IRQ / 32] |= 1u << (SDIO_IRQ % 32);
#else
    enable[VIRTIO_IRQ / 32] |= 1u << (VIRTIO_IRQ % 32);
#endif
#endif
    // 设置响应阈值
    *(volatile uint32 *)PLIC_SPRIORITY(hartid) = 0;
}

// 获取中断号
int plic_claim(void)
{
    int hartid = mycpuid();
    int irq = *(volatile uint32 *)PLIC_SCLAIM(hartid);
    return irq;
}

// 确认该中断号对应中断已经完成
void plic_complete(int irq)
{
    int hartid = mycpuid();
    *(volatile uint32 *)PLIC_SCLAIM(hartid) = irq;
}

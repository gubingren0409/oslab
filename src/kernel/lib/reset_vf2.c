#include "mod.h"

#ifdef VISIONFIVE2
/* JH7110 I2C5: SYSCLK 143, SYSRST 81; PMIC AXP15060 at address 0x36.
 * U-Boot removes the DesignWare controller before entering the kernel.
 * OpenSBI's PMIC reset driver assumes its timing and master mode are ready.
 */
#define I2C5_CLK (VF2_SYSCRG_BASE + 0x23c)
#define I2C5_RESET (VF2_SYSCRG_BASE + 0x300)
#define I2C5_RESET_STATUS (VF2_SYSCRG_BASE + 0x310)
#define RESET_BIT (1u << 17)
#define CLOCK_ENABLE (1u << 31)
#define RESET_TIMEOUT 200000ul /* 50 ms at the board's 4 MHz timebase */

#define IC_CON 0x00
#define IC_TAR 0x04
#define IC_DATA_CMD 0x10
#define IC_SS_HCNT 0x14
#define IC_SS_LCNT 0x18
#define IC_INTR_MASK 0x30
#define IC_RAW_INTR_STAT 0x34
#define IC_RX_TL 0x38
#define IC_TX_TL 0x3c
#define IC_CLR_INTR 0x40
#define IC_ENABLE 0x6c
#define IC_STATUS 0x70
#define IC_SDA_HOLD 0x7c
#define IC_ENABLE_STATUS 0x9c
#define IC_COMP_TYPE 0xfc

static uint32 read32(uint64 address)
{
    asm volatile("fence iorw, iorw" ::: "memory");
    uint32 value = *(volatile uint32 *)address;
    asm volatile("fence iorw, iorw" ::: "memory");
    return value;
}

static void write32(uint64 address, uint32 value)
{
    asm volatile("fence iorw, iorw" ::: "memory");
    *(volatile uint32 *)address = value;
    asm volatile("fence iorw, iorw" ::: "memory");
}

static int wait_bits(uint64 address, uint32 mask, uint32 expected)
{
    uint64 start = r_time();
    do {
        if ((read32(address) & mask) == expected)
            return 0;
    } while (r_time() - start < RESET_TIMEOUT);
    return -1;
}

/* Return the PMIC power-register byte, or a negative preparation error.
 * No kernel locks or printf: also callable from the panic polling loop.
 */
int vf2_reset_prepare(void)
{
    write32(I2C5_CLK, read32(I2C5_CLK) | CLOCK_ENABLE);
    write32(I2C5_RESET, read32(I2C5_RESET) & ~RESET_BIT);
    if (wait_bits(I2C5_RESET_STATUS, RESET_BIT, RESET_BIT) < 0)
        return -1;
    if (read32(VF2_I2C5_BASE + IC_COMP_TYPE) != 0x44570140)
        return -2;

    write32(VF2_I2C5_BASE + IC_ENABLE, 0);
    if (wait_bits(VF2_I2C5_BASE + IC_ENABLE_STATUS, 1, 0) < 0)
        return -3;
    /* Standard-mode master, repeated START, slave disabled. Conservative
     * counts keep the bus below 100 kHz even with a 200 MHz APB clock.
     */
    write32(VF2_I2C5_BASE + IC_CON, 0x63);
    write32(VF2_I2C5_BASE + IC_SS_HCNT, 1000);
    write32(VF2_I2C5_BASE + IC_SS_LCNT, 1200);
    write32(VF2_I2C5_BASE + IC_SDA_HOLD, 0x10040);
    write32(VF2_I2C5_BASE + IC_RX_TL, 0);
    write32(VF2_I2C5_BASE + IC_TX_TL, 0);
    write32(VF2_I2C5_BASE + IC_INTR_MASK, 0);
    (void)read32(VF2_I2C5_BASE + IC_CLR_INTR);
    write32(VF2_I2C5_BASE + IC_TAR, 0x36);
    write32(VF2_I2C5_BASE + IC_ENABLE, 1);
    if (wait_bits(VF2_I2C5_BASE + IC_ENABLE_STATUS, 1, 1) < 0)
        return -4;

    write32(VF2_I2C5_BASE + IC_DATA_CMD, 0x32);
    write32(VF2_I2C5_BASE + IC_DATA_CMD, (1u << 8) | (1u << 9) | (1u << 10));
    uint64 start = r_time();
    while (!(read32(VF2_I2C5_BASE + IC_STATUS) & (1u << 3)))
    {
        if (read32(VF2_I2C5_BASE + IC_RAW_INTR_STAT) & (1u << 6))
            return -5;
        if (r_time() - start >= RESET_TIMEOUT)
            return -6;
    }
    int power = read32(VF2_I2C5_BASE + IC_DATA_CMD) & 0xff;
    if (wait_bits(VF2_I2C5_BASE + IC_STATUS, 1, 0) < 0)
        return -7;
    return power;
}

int vf2_reboot(void)
{
    int power = vf2_reset_prepare();
    if (power < 0)
        return power;
    sbi_ret_t result = sbi_system_reset(1, 0); /* Full PMIC cold reset. */
    return (int)result.error;
}

#ifdef OSLAB_SELFTEST
void vf2_reset_report(void)
{
    printf("VF2_RESET_BEFORE clk=%x reset=%x status=%x con=%x hcnt=%x lcnt=%x\n",
           (uint64)read32(I2C5_CLK), (uint64)read32(I2C5_RESET),
           (uint64)read32(I2C5_RESET_STATUS),
           (uint64)read32(VF2_I2C5_BASE + IC_CON),
           (uint64)read32(VF2_I2C5_BASE + IC_SS_HCNT),
           (uint64)read32(VF2_I2C5_BASE + IC_SS_LCNT));
    printf("VF2_RESET_PREPARE power=%d\n", vf2_reset_prepare());
}
#endif
#endif

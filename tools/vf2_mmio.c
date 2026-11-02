/* Linux diagnostic helper. Build in VMware, load with ctypes on the board.
 * Python's memcpy-based ctypes getters can issue byte reads; SYSCRG requires
 * a single 32-bit access, including its clock-gate and reset status bits.
 */
#include <stdint.h>

uint32_t vf2_mmio_read32(const void *address)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    uint32_t value = *(const volatile uint32_t *)address;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return value;
}

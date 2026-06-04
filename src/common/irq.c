#include "pico/irq.h"

#define VTOR_REG  (*(volatile uint32_t *)0xE000ED08u)
#define NVIC_ISER (*(volatile uint32_t *)0xE000E100u)
#define NVIC_ICER (*(volatile uint32_t *)0xE000E180u)
#define NVIC_ICPR (*(volatile uint32_t *)0xE000E280u)

// 1KB SRAM vector table: covers the largest chip (RP2350: 68 entries = 272 bytes).
// 1KB alignment satisfies VTOR requirements for up to 256 entries.
__attribute__((aligned(1024))) static uint32_t _vtor[256];

// _vectors is defined in start.s — the flash vector table placed at 0x10000100.
extern uint32_t _vectors[];

void irq_init(void) {
    // Read directly from _vectors rather than from VTOR, which may still point
    // to ROM (0x00000000) if the boot sequence hasn't updated it yet.
    for (int i = 0; i < 256; i++)
        _vtor[i] = _vectors[i];

    VTOR_REG = (uint32_t)_vtor;

    // Ensure the VTOR write completes and the processor re-fetches from the
    // new table before any subsequent exception can occur.
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb");
}

void irq_set_handler(uint irq_num, irq_handler_t handler) {
    _vtor[16 + irq_num] = (uint32_t)handler;
}

void irq_enable(uint irq_num) {
    NVIC_ISER = 1u << irq_num;
}

void irq_disable(uint irq_num) {
    NVIC_ICER = 1u << irq_num;
}

void irq_clear_pending(uint irq_num) {
    NVIC_ICPR = 1u << irq_num;
}

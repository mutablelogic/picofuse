#pragma once

#include "pico/types.h"

typedef void (*irq_handler_t)(void);

// Copy the vector table from flash to SRAM and point VTOR at the SRAM copy.
// Must be called before irq_set_handler or irq_enable.
void irq_init(void);

// Install a handler for external IRQ irq_num (0-based, e.g. IO_IRQ_BANK0).
void irq_set_handler(uint irq_num, irq_handler_t handler);

void irq_enable(uint irq_num);
void irq_disable(uint irq_num);
void irq_clear_pending(uint irq_num);

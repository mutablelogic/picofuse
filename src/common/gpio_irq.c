#include "pico/gpio.h"
#include "pico/irq.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/io_bank0.h"

#ifndef PICOFUSE_IO_BANK0_IRQ
#error "PICOFUSE_IO_BANK0_IRQ not defined — include a chip cmake (rp2040.cmake / rp2350.cmake)"
#endif
#ifndef PICOFUSE_NUM_GPIOS
#error "PICOFUSE_NUM_GPIOS not defined — include a chip cmake (rp2040.cmake / rp2350.cmake)"
#endif

#define REG(addr) (*(volatile uint32_t *)(addr))

// Each INTE/INTS/INTR register covers 8 GPIOs, 4 event bits each.
#define _INTE(gpio) REG(IO_BANK0_BASE + IO_BANK0_PROC0_INTE0_OFFSET + ((gpio) / 8u) * 4u)
#define _INTS(gpio) REG(IO_BANK0_BASE + IO_BANK0_PROC0_INTS0_OFFSET + ((gpio) / 8u) * 4u)
#define _INTR(gpio) REG(IO_BANK0_BASE + IO_BANK0_INTR0_OFFSET       + ((gpio) / 8u) * 4u)
#define _SHIFT(gpio) (((gpio) % 8u) * 4u)

static gpio_irq_callback_t _callback;
static bool _handler_installed;

static void _gpio_irq_handler(void) {
    for (uint gpio = 0; gpio < PICOFUSE_NUM_GPIOS; gpio++) {
        uint32_t events = (_INTS(gpio) >> _SHIFT(gpio)) & 0xfu;
        if (events) {
            if (_callback)
                _callback(gpio, events);
            // Acknowledge edge events (level events clear when the pin changes)
            _INTR(gpio) = events << _SHIFT(gpio);
        }
    }
}

void gpio_set_irq_callback(gpio_irq_callback_t callback) {
    _callback = callback;
}

void gpio_set_irq_enabled(uint gpio, uint32_t event_mask, bool enabled) {
    uint32_t bits = (event_mask & 0xfu) << _SHIFT(gpio);
    if (enabled) {
        _INTE(gpio) |= bits;
        if (!_handler_installed) {
            irq_set_handler(PICOFUSE_IO_BANK0_IRQ, _gpio_irq_handler);
            irq_clear_pending(PICOFUSE_IO_BANK0_IRQ);
            irq_enable(PICOFUSE_IO_BANK0_IRQ);
            _handler_installed = true;
        }
    } else {
        _INTE(gpio) &= ~bits;
    }
}

void gpio_acknowledge_irq(uint gpio, uint32_t event_mask) {
    _INTR(gpio) = (event_mask & 0xfu) << _SHIFT(gpio);
}

uint32_t gpio_get_irq_events(uint gpio) {
    return (_INTR(gpio) >> _SHIFT(gpio)) & 0xfu;
}

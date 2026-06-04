#include "pico/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/sio.h"

#define REG(addr)  (*(volatile uint32_t *)(addr))

#define RESETS_RESET_CLR  REG(RESETS_BASE + REG_ALIAS_CLR_BITS)
#define RESETS_RESET_DONE REG(RESETS_BASE + RESETS_RESET_DONE_OFFSET)

// CTRL register for pin n: base + (n * 8) + 4
// Works for all 48 GPIOs (0-47).
#define IO_BANK0_CTRL(n)  REG(IO_BANK0_BASE + IO_BANK0_GPIO0_CTRL_OFFSET + (n) * 8u)

#define PAD_CTRL_SET(n) REG(PADS_BANK0_BASE + REG_ALIAS_SET_BITS + PADS_BANK0_GPIO0_OFFSET + (n) * 4u)
#define PAD_CTRL_CLR(n) REG(PADS_BANK0_BASE + REG_ALIAS_CLR_BITS + PADS_BANK0_GPIO0_OFFSET + (n) * 4u)

// Pins  0-31: GPIO_*    registers, bit = gpio
// Pins 32-47: GPIO_HI_* registers, bit = gpio - 32
#define SIO_GPIO_IN          REG(SIO_BASE + SIO_GPIO_IN_OFFSET)
#define SIO_GPIO_OUT_SET     REG(SIO_BASE + SIO_GPIO_OUT_SET_OFFSET)
#define SIO_GPIO_OUT_CLR     REG(SIO_BASE + SIO_GPIO_OUT_CLR_OFFSET)
#define SIO_GPIO_OUT_XOR     REG(SIO_BASE + SIO_GPIO_OUT_XOR_OFFSET)
#define SIO_GPIO_OE_SET      REG(SIO_BASE + SIO_GPIO_OE_SET_OFFSET)
#define SIO_GPIO_OE_CLR      REG(SIO_BASE + SIO_GPIO_OE_CLR_OFFSET)
#define SIO_GPIO_HI_IN       REG(SIO_BASE + SIO_GPIO_HI_IN_OFFSET)
#define SIO_GPIO_HI_OUT_SET  REG(SIO_BASE + SIO_GPIO_HI_OUT_SET_OFFSET)
#define SIO_GPIO_HI_OUT_CLR  REG(SIO_BASE + SIO_GPIO_HI_OUT_CLR_OFFSET)
#define SIO_GPIO_HI_OUT_XOR  REG(SIO_BASE + SIO_GPIO_HI_OUT_XOR_OFFSET)
#define SIO_GPIO_HI_OE_SET   REG(SIO_BASE + SIO_GPIO_HI_OE_SET_OFFSET)
#define SIO_GPIO_HI_OE_CLR   REG(SIO_BASE + SIO_GPIO_HI_OE_CLR_OFFSET)

static void _unreset_gpio(void) {
    uint32_t bits = RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS;
    RESETS_RESET_CLR = bits;
    while (!(RESETS_RESET_DONE & bits));
}

void gpio_set_function(uint gpio, gpio_func_t fn) {
    PAD_CTRL_SET(gpio) = PADS_BANK0_GPIO0_IE_BITS;
    // ISO is set at reset on RP2350 and isolates the pad until cleared
    PAD_CTRL_CLR(gpio) = PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_ISO_BITS;
    IO_BANK0_CTRL(gpio) = (uint32_t)fn;
}

void gpio_init(uint gpio) {
    _unreset_gpio();
    gpio_put(gpio, false);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_set_function(gpio, GPIO_FUNC_SIO);
}

void gpio_set_dir(uint gpio, gpio_dir_t dir) {
    uint32_t bit = 1u << (gpio < 32 ? gpio : gpio - 32);
    if (gpio < 32) {
        if (dir == GPIO_OUT) SIO_GPIO_OE_SET    = bit;
        else                 SIO_GPIO_OE_CLR    = bit;
    } else {
        if (dir == GPIO_OUT) SIO_GPIO_HI_OE_SET = bit;
        else                 SIO_GPIO_HI_OE_CLR = bit;
    }
}

void gpio_put(uint gpio, bool value) {
    uint32_t bit = 1u << (gpio < 32 ? gpio : gpio - 32);
    if (gpio < 32) {
        if (value) SIO_GPIO_OUT_SET    = bit;
        else       SIO_GPIO_OUT_CLR    = bit;
    } else {
        if (value) SIO_GPIO_HI_OUT_SET = bit;
        else       SIO_GPIO_HI_OUT_CLR = bit;
    }
}

bool gpio_get(uint gpio) {
    if (gpio < 32)
        return (SIO_GPIO_IN    >> gpio)        & 1u;
    else
        return (SIO_GPIO_HI_IN >> (gpio - 32)) & 1u;
}

void gpio_toggle(uint gpio) {
    uint32_t bit = 1u << (gpio < 32 ? gpio : gpio - 32);
    if (gpio < 32) SIO_GPIO_OUT_XOR    = bit;
    else           SIO_GPIO_HI_OUT_XOR = bit;
}

void gpio_pull_up(uint gpio) {
    PAD_CTRL_CLR(gpio) = PADS_BANK0_GPIO0_PDE_BITS;
    PAD_CTRL_SET(gpio) = PADS_BANK0_GPIO0_PUE_BITS;
}

void gpio_pull_down(uint gpio) {
    PAD_CTRL_CLR(gpio) = PADS_BANK0_GPIO0_PUE_BITS;
    PAD_CTRL_SET(gpio) = PADS_BANK0_GPIO0_PDE_BITS;
}

void gpio_disable_pulls(uint gpio) {
    PAD_CTRL_CLR(gpio) = PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS;
}

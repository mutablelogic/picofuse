#pragma once

#include "pico/types.h"

typedef enum {
    GPIO_FUNC_SIO  = 5,
    GPIO_FUNC_UART = 2,
    GPIO_FUNC_NULL = 31,
} gpio_func_t;

typedef enum {
    GPIO_IN  = 0,
    GPIO_OUT = 1,
} gpio_dir_t;

void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, gpio_dir_t dir);
void gpio_put(uint gpio, bool value);
bool gpio_get(uint gpio);
void gpio_toggle(uint gpio);
void gpio_set_function(uint gpio, gpio_func_t fn);
void gpio_pull_up(uint gpio);
void gpio_pull_down(uint gpio);
void gpio_disable_pulls(uint gpio);

// GPIO interrupt events (combinable with |)
#define GPIO_IRQ_LEVEL_LOW  0x1u
#define GPIO_IRQ_LEVEL_HIGH 0x2u
#define GPIO_IRQ_EDGE_FALL  0x4u
#define GPIO_IRQ_EDGE_RISE  0x8u

typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t events);

// Register a callback invoked from the IO_BANK0 ISR for all GPIO events.
// Call irq_init() before this.
void gpio_set_irq_callback(gpio_irq_callback_t callback);

// Enable or disable interrupt events on a GPIO pin.
void gpio_set_irq_enabled(uint gpio, uint32_t event_mask, bool enabled);

// Acknowledge edge-triggered events (call from within the callback).
void gpio_acknowledge_irq(uint gpio, uint32_t event_mask);

// Poll the raw interrupt status for a GPIO (ignores INTE — useful for debug).
uint32_t gpio_get_irq_events(uint gpio);

#pragma once

#include "pico/types.h"

typedef enum {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN = 1,
    UART_PARITY_ODD  = 2,
} uart_parity_t;

// Initialise UART (instance 0 or 1), configure tx/rx pins, set baudrate (8N1).
void uart_init(uint instance, uint tx_pin, uint rx_pin, uint baudrate);

// Set data format.
void uart_set_format(uint instance, uint data_bits, uint stop_bits, uart_parity_t parity);

// Write one character, blocking until TX FIFO has space.
void uart_putc(uint instance, char c);

// Write a null-terminated string.
void uart_puts(uint instance, const char *s);

// Read one character, blocking until one is available.
char uart_getc(uint instance);

// True if the RX FIFO has data.
bool uart_is_readable(uint instance);

// True if the TX FIFO has space.
bool uart_is_writable(uint instance);

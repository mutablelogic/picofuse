#include "pico/uart.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/uart.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

#define RESETS_RESET_CLR REG(RESETS_BASE + REG_ALIAS_CLR_BITS)
#define RESETS_RESET_DONE REG(RESETS_BASE + RESETS_RESET_DONE_OFFSET)

static const uint32_t _base[] = {UART0_BASE, UART1_BASE};
static const uint32_t _reset[] = {RESETS_RESET_UART0_BITS,
                                  RESETS_RESET_UART1_BITS};

#define UREG(inst, off) REG(_base[inst] + (off))

void uart_init(uint instance, uint tx_pin, uint rx_pin, uint baudrate) {
  uint32_t reset_bits = _reset[instance] | RESETS_RESET_IO_BANK0_BITS;
  RESETS_RESET_CLR = reset_bits;
  while (!(RESETS_RESET_DONE & reset_bits))
    ;

  UREG(instance, UART_UARTCR_OFFSET) = 0;

  // BAUDDIV * 64 = (clk * 4) / baudrate, rounded
  uint32_t baud64 = (4u * PICOFUSE_SYS_CLK_HZ + baudrate / 2u) / baudrate;
  UREG(instance, UART_UARTIBRD_OFFSET) = baud64 >> 6;
  UREG(instance, UART_UARTFBRD_OFFSET) = baud64 & 0x3fu;

  // 8N1, FIFOs enabled (must write LCR_H after baud registers)
  UREG(instance, UART_UARTLCR_H_OFFSET) =
      UART_UARTLCR_H_WLEN_BITS | UART_UARTLCR_H_FEN_BITS;

  UREG(instance, UART_UARTCR_OFFSET) =
      UART_UARTCR_UARTEN_BITS | UART_UARTCR_TXE_BITS | UART_UARTCR_RXE_BITS;

  // Set GPIO pins to UART function (FUNCSEL = 2)
  REG(IO_BANK0_BASE + IO_BANK0_GPIO0_CTRL_OFFSET + tx_pin * 8u) = 2u;
  REG(IO_BANK0_BASE + IO_BANK0_GPIO0_CTRL_OFFSET + rx_pin * 8u) = 2u;
}

void uart_set_format(uint instance, uint data_bits, uint stop_bits,
                     uart_parity_t parity) {
  uint32_t lcr = UREG(instance, UART_UARTLCR_H_OFFSET);
  lcr &= ~(UART_UARTLCR_H_WLEN_BITS | 0x38u); // clear WLEN, STP2, PEN, EPS
  lcr |= ((data_bits - 5u) & 0x3u) << 5u;
  if (stop_bits == 2)
    lcr |= (1u << 3);
  if (parity != UART_PARITY_NONE) {
    lcr |= (1u << 1); // PEN
    if (parity == UART_PARITY_EVEN)
      lcr |= (1u << 2); // EPS
  }
  UREG(instance, UART_UARTLCR_H_OFFSET) = lcr;
}

bool uart_is_writable(uint instance) {
  return !(UREG(instance, UART_UARTFR_OFFSET) & UART_UARTFR_TXFF_BITS);
}

bool uart_is_readable(uint instance) {
  return !(UREG(instance, UART_UARTFR_OFFSET) & UART_UARTFR_RXFE_BITS);
}

void uart_putc(uint instance, char c) {
  while (!uart_is_writable(instance))
    ;
  UREG(instance, UART_UARTDR_OFFSET) = (uint32_t)c;
}

void uart_puts(uint instance, const char *s) {
  while (*s)
    uart_putc(instance, *s++);
}

char uart_getc(uint instance) {
  while (!uart_is_readable(instance))
    ;
  return (char)(UREG(instance, UART_UARTDR_OFFSET) & 0xffu);
}

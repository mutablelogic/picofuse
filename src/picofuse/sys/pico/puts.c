#include <picofuse/sys.h>

// sys_iostream_write() documents a short (including zero) write as a valid
// outcome - e.g. sys/pico/stdio_uart.c's ring buffer returns 0 once full,
// relying on its IRQ handler to drain it as the real UART FIFO empties.
// Both functions here used to call it once and ignore the result, silently
// dropping whatever didn't fit - now they retry until every byte is
// actually accepted. sys_stdout == NULL is the one case a write can never
// succeed, so it's checked upfront rather than retried forever.

void sys_puts(const char *str) {
  if (str == NULL || sys_stdout == NULL) {
    return;
  }

  size_t len = 0;
  while (str[len] != '\0') {
    len++;
  }

  size_t written = 0;
  while (written < len) {
    written += sys_iostream_write(sys_stdout, str + written, len - written);
  }
}

void sys_putch(const char ch) {
  if (ch == '\0' || sys_stdout == NULL) {
    return;
  }

  while (sys_iostream_write(sys_stdout, &ch, 1) == 0) {
  }
}

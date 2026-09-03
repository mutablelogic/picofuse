#include <picofuse/sys.h>

// sys_iostream_write() documents a short (including zero) write as a valid
// outcome - e.g. sys/pico/stdio_uart.c's ring buffer returns 0 once full,
// relying on its IRQ handler to drain it as the real UART FIFO empties.
// Both functions here used to call it once and ignore the result, silently
// dropping whatever didn't fit - now they retry until every byte is
// actually accepted, or SYS_PUTS_RETRY_TIMEOUT_MS elapses.
#ifndef SYS_PUTS_RETRY_TIMEOUT_MS
#define SYS_PUTS_RETRY_TIMEOUT_MS 250
#endif

void sys_puts(const char *str) {
  if (str == NULL || sys_stdout == NULL) {
    return;
  }

  size_t len = 0;
  while (str[len] != '\0') {
    len++;
  }

  uint64_t deadline_ms = sys_timestamp_ms() + SYS_PUTS_RETRY_TIMEOUT_MS;
  size_t written = 0;
  while (written < len) {
    size_t n = sys_iostream_write(sys_stdout, str + written, len - written);
    if (n > 0) {
      written += n;
      continue;
    }
    if (sys_timestamp_ms() >= deadline_ms) {
      return; // give up - stream isn't draining
    }
  }
}

void sys_putch(const char ch) {
  if (ch == '\0' || sys_stdout == NULL) {
    return;
  }

  uint64_t deadline_ms = sys_timestamp_ms() + SYS_PUTS_RETRY_TIMEOUT_MS;
  while (sys_iostream_write(sys_stdout, &ch, 1) == 0) {
    if (sys_timestamp_ms() >= deadline_ms) {
      return; // give up - stream isn't draining
    }
  }
}

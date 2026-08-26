#include <hardware/uart.h>
#include <picofuse/sys.h>
#include <runtime/stdout.h>

void sys_puts(const char *str) {
  if (sys_stdout != NULL && str != NULL && *str != '\0') {
    uart_puts((uart_inst_t *)sys_stdout, str);
  }
}

void sys_putch(const char ch) {
  if (sys_stdout != NULL && ch != '\0') {
    uart_putc((uart_inst_t *)sys_stdout, ch);
  }
}

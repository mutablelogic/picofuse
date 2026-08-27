#include <hardware/uart.h>
#include <picofuse/sys.h>
#include <runtime/stdout.h>

void sys_init(void) {
  sys_stdout = uart_get_instance(0);
  sys_assert(sys_stdout != NULL);
  uart_init((uart_inst_t *)sys_stdout, 115200);
}

void sys_exit(void) {
  sys_puts("\n[HALT]\n");
  uart_deinit((uart_inst_t *)sys_stdout);
  sys_stdout = NULL;

  while (1) {
    /* no-op */
  }
}

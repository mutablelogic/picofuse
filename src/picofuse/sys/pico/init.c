#include "../printf/mutex.h"
#include <hardware/uart.h>
#include <picofuse/sys.h>
#include <runtime/stdout.h>

void sys_init(void) {
  sys_stdout = uart_get_instance(0);
  sys_assert(sys_stdout != NULL);
  uart_init((uart_inst_t *)sys_stdout, 115200);

  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();
}

void sys_exit(void) {
  /* TODO: Shutdown or reset any non-main cores */
  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();

  sys_puts("\n[HALT]\n");
  uart_deinit((uart_inst_t *)sys_stdout);
  sys_stdout = NULL;

  while (1) {
    /* no-op */
  }
}

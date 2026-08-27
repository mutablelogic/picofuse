#include "../printf/mutex.h"
#include "mutex.h"
#include <hardware/uart.h>
#include <picofuse/sys.h>
#include <runtime/stdout.h>

void sys_init(void) {
  sys_stdout = uart_get_instance(0);
  sys_assert(sys_stdout != NULL);
  uart_init((uart_inst_t *)sys_stdout, 115200);

  // Initialize the mutex subsystem
  _sys_mutex_module_init();

  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();

  // Set the timestamp base for relative timing
  sys_timestamp_ms();
}

void sys_exit(void) {
  /* TODO: Shutdown or reset any non-main cores */
  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();

  // Deinitialize the mutex subsystem
  _sys_mutex_module_deinit();

  // Print a message indicating that the system is halting
  sys_puts("\n[HALT]\n");
  uart_deinit((uart_inst_t *)sys_stdout);
  sys_stdout = NULL;

  // Halt the system and never return
  sys_halt();
}

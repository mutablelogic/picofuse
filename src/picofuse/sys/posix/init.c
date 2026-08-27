#include "../printf/mutex.h"
#include <picofuse/sys.h>

void sys_init(void) {
  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();
  // Set the timestamp base for relative timing
  sys_timestamp_ms();
}

void sys_exit(void) {
  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();
}

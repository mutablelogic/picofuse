#include "../printf/mutex.h"
#include <picofuse/sys.h>

void sys_init(void) {
  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();
}

void sys_exit(void) {
  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();
}

#include "mutex.h"
#include <picofuse/sys.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Global mutex for thread-safe printf operations
sys_mutex_t *_sys_printf_mutex = NULL;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

bool _sys_printf_init(void) {
  _sys_printf_mutex = sys_mutex_init();
  return _sys_printf_mutex != NULL;
}

void _sys_printf_exit(void) {
  if (_sys_printf_mutex != NULL) {
    sys_mutex_deinit(_sys_printf_mutex);
    _sys_printf_mutex = NULL;
  }
}

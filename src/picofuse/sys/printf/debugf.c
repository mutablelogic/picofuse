#include "mutex.h"
#include "types.h"
#include <picofuse/sys.h>
#include <stdarg.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

#ifndef NDEBUG
void _sys_debugf_impl(const char *context, const char *format, ...) {
  sys_mutex_lock(_sys_printf_mutex);
  sys_puts("[DEBUG] ");
  if (context != NULL) {
    sys_putch('[');
    sys_puts(context);
    sys_putch(']');
    sys_putch(' ');
  }

  struct sys_printf_state state = {.putch = _sys_printf_putch, .custom = NULL};
  va_list va;
  va_start(va, format);
  _sys_vprintf(&state, format, &va);
  va_end(va);

  sys_mutex_unlock(_sys_printf_mutex);
}
#endif

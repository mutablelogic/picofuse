#include "mutex.h"
#include "types.h"
#include <picofuse/sys.h>
#include <stdarg.h>
#include <string.h>

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

  // Callers routinely omit a trailing "\n" (see e.g. src/picofuse/hw/pico/
  // adc.c's sys_debugf() calls) - without one, consecutive debug lines run
  // together on the same line, which among other things can hide a
  // "[TEST] [EXIT] "/"[PANIC] " marker that testrunner's line-based search
  // is waiting for. Only the literal format string is checked, not
  // whatever a %s argument happens to expand to.
  size_t len = strlen(format);
  if (len == 0 || format[len - 1] != '\n') {
    sys_putch('\n');
  }

  sys_mutex_unlock(_sys_printf_mutex);
}
#endif

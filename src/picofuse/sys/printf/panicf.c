#include "mutex.h"
#include "types.h"
#include <picofuse/sys.h>
#include <stdarg.h>

_Noreturn void sys_panicf(const char *format, ...) {
  sys_mutex_lock(_sys_printf_mutex);
  sys_puts("[PANIC] ");

  struct sys_printf_state state = {.putch = _sys_printf_putch, .custom = NULL};
  va_list va;
  va_start(va, format);
  _sys_vprintf(&state, format, &va);
  va_end(va);

  sys_puts("\n");
  sys_mutex_unlock(_sys_printf_mutex);
  sys_halt();
}

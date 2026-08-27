#include "../printf/mutex.h"
#include "../printf/types.h"
#include <picofuse/sys.h>
#include <stdarg.h>
#include <stdlib.h>

void sys_panicf(const char *format, ...) {
  // Locked for the whole message, not just the sys_vprintf() portion:
  // sys_puts() never takes the printf mutex on its own, so without this a
  // concurrent sys_printf() on another thread could interleave with the
  // "[PANIC] " tag or trailing newline.
  sys_mutex_lock(_sys_printf_mutex);

  sys_puts("[PANIC] ");

  struct sys_printf_state state = {.putch = _sys_printf_putch, .custom = NULL};
  va_list va;
  va_start(va, format);
  _sys_vprintf(&state, format, &va);
  va_end(va);

  sys_puts("\n");

  sys_mutex_unlock(_sys_printf_mutex);

  // abort() rather than exit(): a panic is a fatal programming error, not a
  // graceful shutdown, and abort()'s core dump/signal is what a developer
  // attached to the process actually wants to see.
  abort();
}

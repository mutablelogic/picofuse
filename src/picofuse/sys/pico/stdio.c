#include <picofuse/sys.h>

extern void _sys_stdio_uart_init(void);
extern void _sys_stdio_uart_exit(void);

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

sys_iostream_t *sys_stdout = NULL;
sys_iostream_t *sys_stdin = NULL;

///////////////////////////////////////////////////////////////////////////////
// MODULE METHODS

void _sys_stdio_module_init(sys_stdio_t type) {
  if (type != sys_stdio_none && type != sys_stdio_uart) {
    return;
  }
  _sys_stdio_uart_init();
}

void _sys_stdio_module_exit(void) { _sys_stdio_uart_exit(); }
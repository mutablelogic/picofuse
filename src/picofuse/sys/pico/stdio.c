#include <picofuse/sys.h>

extern void _sys_stdio_uart_init(void);
extern void _sys_stdio_uart_exit(void);
extern void _sys_stdio_rtt_init(void);
extern void _sys_stdio_rtt_exit(void);

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

sys_iostream_t *sys_stdout = NULL;
sys_iostream_t *sys_stdin = NULL;
static sys_stdio_t _sys_stdio_type = sys_stdio_none;

///////////////////////////////////////////////////////////////////////////////
// MODULE METHODS

void _sys_stdio_module_init(sys_stdio_t type) {
  if (type == sys_stdio_none) {
    type = sys_stdio_uart;
  }

  switch (type) {
  case sys_stdio_uart:
    _sys_stdio_uart_init();
    break;
  case sys_stdio_rtt:
    _sys_stdio_rtt_init();
    break;
  default:
    return;
  }
  _sys_stdio_type = type;
}

void _sys_stdio_module_exit(void) {
  switch (_sys_stdio_type) {
  case sys_stdio_uart:
    _sys_stdio_uart_exit();
    break;
  case sys_stdio_rtt:
    _sys_stdio_rtt_exit();
    break;
  default:
    break;
  }
  _sys_stdio_type = sys_stdio_none;
}
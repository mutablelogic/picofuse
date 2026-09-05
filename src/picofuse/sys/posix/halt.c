#include <picofuse/sys.h>
#include <stdlib.h>

// abort() terminates immediately with no unwinding, so a panic (sys_panicf()
// -> sys_halt(), e.g. from a failed test_assert()) never reaches sys_exit()
// and its usual cleanup. Restore the tty here instead, or every test/app run
// interactively leaves its shell with no echo after a panic.
extern void _sys_stdio_restore_terminal(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Halts the system by aborting the program.
 * @note This function does not return.
 */
_Noreturn void sys_halt(void) {
  _sys_stdio_restore_terminal();
  abort();
}

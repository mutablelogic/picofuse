#include <picofuse/sys.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Halts the system by aborting the program.
 * @note This function does not return.
 */
_Noreturn void sys_halt(void) { abort(); }

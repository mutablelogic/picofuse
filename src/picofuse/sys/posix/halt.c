#include <picofuse/sys.h>
#include <stdlib.h>

_Noreturn void sys_halt(void) { abort(); }

#include "../env/env.h"
#include "../printf/mutex.h"
#include <picofuse/sys.h>

// Defined per-platform in darwin/timer.c and linux/timer.c.
extern void _sys_timer_module_exit(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Initializes the system.
 * @note This function should be called before any other system functions.
 */
void sys_init(int argc, char *argv[]) {
  // Capture argc/argv for sys_env_args_parse()
  _sys_env_set_args(argc, argv);
  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();
  // Set the timestamp base for relative timing
  sys_timestamp_ms();
}

/**
 * @brief Deinitializes the system.
 * @note This function should be called when the system is no longer needed.
 */
void sys_exit(void) {
  // Stop and release any still-active timers
  _sys_timer_module_exit();
  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();
}

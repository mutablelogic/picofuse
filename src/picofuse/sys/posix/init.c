#include "../env/env.h"
#include "../printf/mutex.h"
#include <picofuse/sys.h>
#include <stdlib.h>

// Defined in posix/stdio.c.
extern void _sys_stdio_module_init(sys_stdio_t type);
extern void _sys_stdio_module_exit(void);

// Defined per-platform in darwin/timer.c and linux/timer.c.
extern void _sys_timer_module_exit(void);

// Defined in mem/mem.c.
extern bool _sys_mem_module_init(size_t capacity, void *(*malloc_fn)(size_t),
                                 void (*free_fn)(void *));
extern void _sys_mem_module_exit(void);

// Defined in event/runloop.c.
extern bool _sys_runloop_module_init(void);
extern void _sys_runloop_module_exit(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Initializes the system.
 * @note This function should be called before any other system functions.
 */
void sys_init(int argc, char *argv[], size_t arena_size) {
  // Capture argc/argv for sys_env_args_parse()
  _sys_env_set_args(argc, argv);
  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();
  // Set up standard input and output streams
  _sys_stdio_module_init(sys_stdio_none);
  // Set the timestamp base for relative timing
  sys_timestamp_ms();
  // Configure the default arena backing sys_malloc() and friends, if asked
  _sys_mem_module_init(arena_size, malloc, free);
  // Create the mutex guarding the run loop singleton
  _sys_runloop_module_init();
}

/**
 * @brief Deinitializes the system.
 * @note This function should be called when the system is no longer needed.
 */
void sys_exit(void) {
  // Release the mutex guarding the run loop singleton
  _sys_runloop_module_exit();
  // Tear down the default arena, if one was configured
  _sys_mem_module_exit();
  // Stop and release any still-active timers
  _sys_timer_module_exit();
  // Release the standard input and output streams
  _sys_stdio_module_exit();
  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();
}

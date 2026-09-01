#include "../printf/mutex.h"
#include "sync.h"
#include <picofuse/sys.h>
#include <stdlib.h>

// Defined in stdio.c.
extern void _sys_stdio_module_init(sys_stdio_t type);
extern void _sys_stdio_module_exit(void);

// Defined in timer.c.
extern void _sys_timer_module_init(void);
extern void _sys_timer_module_exit(void);

// Defined in mem/mem.c.
extern bool _sys_mem_module_init(size_t capacity, void *(*malloc_fn)(size_t),
                                 void (*free_fn)(void *));
extern void _sys_mem_module_exit(void);

// Defined in event/runloop.c.
extern bool _sys_runloop_module_init(void);
extern void _sys_runloop_module_exit(void);

void sys_init(int argc, char *argv[], size_t arena_size, sys_stdio_t stdio) {
  // Pico has no command line arguments
  (void)argc;
  (void)argv;

  // Configure the default arena backing sys_malloc() and friends, if asked
  _sys_mem_module_init(arena_size, malloc, free);

  // Set up standard input and output streams
  _sys_stdio_module_init(stdio);

  // Initialize the shared critical section used by sync primitives
  _sys_sync_module_init();

  // Create the mutex guarding the run loop singleton (depends on the sync
  // module's critical section above, since sys_mutex_init() uses it)
  _sys_runloop_module_init();

  // Initialize the critical section used by the timer pool
  _sys_timer_module_init();

  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();

  // Set the timestamp base for relative timing
  sys_timestamp_ms();
}

void sys_exit(void) {
  // Tear down the default arena, if one was configured
  _sys_mem_module_exit();

  // Stop and release any still-active timers
  _sys_timer_module_exit();

  // Print a message indicating that the system is halting
  sys_puts("\n[HALT]\n");

  // Release the standard input and output streams
  _sys_stdio_module_exit();

  /* TODO: Shutdown or reset any non-main cores */

  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();

  // Release the mutex guarding the run loop singleton (before the sync
  // module's critical section it depends on goes away)
  _sys_runloop_module_exit();

  // Deinitialize the shared critical section used by sync primitives
  _sys_sync_module_deinit();

  // Halt the system and never return
  sys_halt();
}

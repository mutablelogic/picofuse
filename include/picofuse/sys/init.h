/**
 * @file sys/init.h
 * @ingroup System
 * @brief System initialization and cleanup hooks.
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Lifecycle
 * @{ */

/**
 * @brief Initializes the system on startup.
 * @ingroup System
 * @param argc Argument count, as passed to `main()`.
 * @param argv Argument vector, as passed to `main()`. On platforms with no
 * real command line (such as Pico), pass `0`/`NULL`; nothing reads them.
 * @param arena_size Capacity in bytes for the default arena backing
 * sys_malloc()/sys_calloc()/sys_realloc()/sys_free() (see sys/mem.h), or `0`
 * to leave them routed straight through to the system allocator instead.
 */
void sys_init(int argc, char *argv[], size_t arena_size);

/**
 * @brief Cleans up the system on shutdown.
 * @ingroup System
 */
void sys_exit(void);

/** @} */

#ifdef __cplusplus
}
#endif

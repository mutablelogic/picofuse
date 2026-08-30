/**
 * @file sys/init.h
 * @ingroup System
 * @brief System initialization and cleanup hooks.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the system on startup.
 * @param argc Argument count, as passed to `main()`.
 * @param argv Argument vector, as passed to `main()`. On platforms with no
 * real command line (such as Pico), pass `0`/`NULL`; nothing reads them.
 */
void sys_init(int argc, char *argv[]);

/**
 * @brief Cleans up the system on shutdown.
 */
void sys_exit(void);

#ifdef __cplusplus
}
#endif

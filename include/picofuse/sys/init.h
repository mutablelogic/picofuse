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
 */
void sys_init(void);

/**
 * @brief Cleans up the system on shutdown.
 */
void sys_exit(void);

#ifdef __cplusplus
}
#endif

#pragma once

/**
 * @brief Initializes the critical section shared by the mutex, condition
 * variable, and wait-group pools, and by event queues' ring-buffer
 * bookkeeping (see sys/event/lock.c).
 */
extern void _sys_sync_module_init(void);

/**
 * @brief Deinitializes the shared critical section.
 */
extern void _sys_sync_module_deinit(void);

/**
 * @brief Enters the shared critical section, blocking until available.
 */
extern void _sys_sync_pool_lock(void);

/**
 * @brief Exits the shared critical section.
 */
extern void _sys_sync_pool_unlock(void);

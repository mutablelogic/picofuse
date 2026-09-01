/**
 * @file sys/mutex.h
 * @brief Defines mutex primitives for thread synchronization.
 * @defgroup SystemSync Synchronization
 * @ingroup System
 *
 * Mutexes, condition variables, wait groups, and atomic values used to
 * coordinate access to shared state across threads.
 */

/**
 * @brief Mutex primitives.
 * @defgroup SystemSyncMutex Mutex
 * @ingroup SystemSync
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @def SYS_MUTEX_CAPACITY
 * @ingroup SystemSyncMutex
 * @brief Maximum number of mutexes available in static-pool implementations.
 */
#ifndef SYS_MUTEX_CAPACITY
#define SYS_MUTEX_CAPACITY 32
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Mutex.
 * @ingroup SystemSyncMutex
 * @headerfile mutex.h picofuse/sys.h
 */
typedef struct sys_mutex_t sys_mutex_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a new mutex
 * @ingroup SystemSyncMutex
 * @return Initialized mutex structure
 *
 * Creates and initializes a new mutex for thread synchronization.
 * The mutex is initially unlocked and ready for use. The returned mutex can
 * be released with sys_mutex_deinit(). Implementations that use a static pool
 * may return `NULL` after `SYS_MUTEX_CAPACITY` mutexes have been allocated.
 */
sys_mutex_t *sys_mutex_init(void);

/**
 * @brief Release a mutex
 * @ingroup SystemSyncMutex
 * @param mutex Pointer to the mutex to release
 *
 * Releases all resources associated with the mutex and renders it
 * unusable. The mutex should not be locked when this function is called.
 */
void sys_mutex_deinit(sys_mutex_t *mutex);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Lock a mutex, by blocking
 * @ingroup SystemSyncMutex
 * @param mutex Pointer to the mutex to lock
 * @return true if the mutex was successfully locked, false on error
 *
 * Attempts to lock the specified mutex. If the mutex is already locked
 * by another thread, this function will block until the mutex becomes
 * available. Every successful lock must be paired with sys_mutex_unlock()
 * Attempting to lock an already owned mutex may result in deadlock
 */
bool sys_mutex_lock(sys_mutex_t *mutex);

/**
 * @brief Try to lock a mutex
 * @ingroup SystemSyncMutex
 * @param mutex Pointer to the mutex to try locking
 * @return true if the mutex was successfully locked, false if already locked
 * or on error
 *
 * Attempts to lock the specified mutex without blocking. If the mutex
 * is already locked, this function returns immediately with false.
 * Every successful trylock must be paired with sys_mutex_unlock()
 */
bool sys_mutex_trylock(sys_mutex_t *mutex);

/**
 * @brief Unlock a mutex
 * @ingroup SystemSyncMutex
 * @param mutex Pointer to the mutex to unlock
 * @return true if the mutex was successfully unlocked, false on error
 *
 * Releases a previously acquired mutex lock, allowing other threads
 * to acquire the mutex. Only the thread that locked the mutex should unlock
 * it.
 */
bool sys_mutex_unlock(sys_mutex_t *mutex);

/** @} */

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

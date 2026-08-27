/**
 * @file sys/cond.h
 * @brief Defines condition variable primitives for thread synchronization.
 * @defgroup SystemSyncCond Condition Variables
 * @ingroup SystemSync
 */

#pragma once
#include "mutex.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @def SYS_COND_CAPACITY
 * @ingroup SystemSyncCond
 * @brief Maximum number of condition variables available in static-pool
 * implementations.
 */
#ifndef SYS_COND_CAPACITY
#define SYS_COND_CAPACITY 32
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Condition variable.
 * @ingroup SystemSyncCond
 * @headerfile cond.h picofuse/sys.h
 */
typedef struct sys_cond_t sys_cond_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a new condition variable
 * @ingroup SystemSyncCond
 * @return Initialized condition variable structure
 *
 * Creates and initializes a new condition variable for thread
 * synchronization. The condition variable is ready for use with wait/signal
 * operations. The returned condition variable must be deinitialized with
 * sys_cond_deinit()
 */
sys_cond_t *sys_cond_init(void);

/**
 * @brief Deinitialize a condition variable
 * @ingroup SystemSyncCond
 * @param cond Pointer to the condition variable to deinitialize
 *
 * Releases all resources associated with the condition variable and renders
 * it unusable. No threads should be waiting on the condition variable when
 * this function is called.
 */
void sys_cond_deinit(sys_cond_t *cond);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Wait on a condition variable
 * @ingroup SystemSyncCond
 * @param cond Pointer to the condition variable to wait on
 * @param mutex Pointer to the mutex that must be locked by the calling thread
 * @return true if the wait completed successfully, false on error
 *
 * Atomically releases the mutex and waits for the condition variable to be
 * signaled. Upon return, the mutex is reacquired. The mutex must be locked
 * by the calling thread before calling this function.
 */
bool sys_cond_wait(sys_cond_t *cond, sys_mutex_t *mutex);

/**
 * @brief Wait on a condition variable with timeout
 * @ingroup SystemSyncCond
 * @param cond Pointer to the condition variable to wait on
 * @param mutex Pointer to the mutex that must be locked by the calling thread
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return true if signaled, false if timeout or error
 *
 * Like sys_cond_wait() but returns after timeout_ms milliseconds if not
 * signaled. Returns true if signaled, false if timeout or error occurred.
 */
bool sys_cond_timedwait(sys_cond_t *cond, sys_mutex_t *mutex,
                        uint32_t timeout_ms);

/**
 * @brief Signal one waiting thread
 * @ingroup SystemSyncCond
 * @param cond Pointer to the condition variable to signal
 * @return true if successful, false on error
 *
 * Wakes up one thread waiting on the condition variable. If no threads
 * are waiting, this function has no effect. The associated mutex should
 * be locked when calling this function for predictable behavior.
 */
bool sys_cond_signal(sys_cond_t *cond);

/**
 * @brief Signal all waiting threads
 * @ingroup SystemSyncCond
 * @param cond Pointer to the condition variable to broadcast
 * @return true if successful, false on error
 *
 * Wakes up all threads waiting on the condition variable. If no threads
 * are waiting, this function has no effect. The associated mutex should
 * be locked when calling this function for predictable behavior.
 */
bool sys_cond_broadcast(sys_cond_t *cond);

/** @} */

#ifdef __cplusplus
}
#endif

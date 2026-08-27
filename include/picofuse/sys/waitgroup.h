/**
 * @file sys/waitgroup.h
 * @brief Defines wait-group primitives for coordinating worker completion.
 * @defgroup SystemSyncWaitgroup Wait Groups
 * @ingroup SystemSync
 */

#pragma once

#include <stdbool.h>

/**
 * @def SYS_WAITGROUP_CAPACITY
 * @ingroup SystemSyncWaitgroup
 * @brief Maximum number of wait groups available in static-pool
 * implementations.
 */
#ifndef SYS_WAITGROUP_CAPACITY
#define SYS_WAITGROUP_CAPACITY 32
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Wait group.
 * @ingroup SystemSyncWaitgroup
 * @headerfile waitgroup.h picofuse/sys.h
 */
typedef struct sys_waitgroup_t sys_waitgroup_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a new wait group
 * @ingroup SystemSyncWaitgroup
 * @return Initialized wait group structure
 *
 * Creates and initializes a new wait group for thread synchronization.
 * The wait group counter starts at 0. The returned wait group is ready for
 * use with sys_waitgroup_add(), sys_waitgroup_done(), and
 * sys_waitgroup_wait(). Implementations that use a static pool may return
 * `NULL` after `SYS_WAITGROUP_CAPACITY` wait groups have been allocated.
 */
sys_waitgroup_t *sys_waitgroup_init(void);

/**
 * @brief Deinitialize a wait group
 * @ingroup SystemSyncWaitgroup
 * @param wg Pointer to the wait group to deinitialize
 *
 * Releases all resources associated with the wait group and renders it
 * unusable. The counter should be 0 and no threads should be waiting in
 * sys_waitgroup_wait() when this function is called.
 */
void sys_waitgroup_deinit(sys_waitgroup_t *wg);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Add to the wait group counter
 * @ingroup SystemSyncWaitgroup
 * @param wg Pointer to the wait group
 * @param delta Number to add to the counter
 * @return true if successful, false on error
 *
 * Increments the wait group counter by delta. This should be called before
 * starting worker threads that the wait group should wait for. Implementations
 * may reject negative deltas.
 */
bool sys_waitgroup_add(sys_waitgroup_t *wg, int delta);

/**
 * @brief Decrement the wait group counter
 * @ingroup SystemSyncWaitgroup
 * @param wg Pointer to the wait group
 * @return true if successful, false on error
 *
 * Decrements the wait group counter by 1. This should be called when a worker
 * finishes its work. If the counter reaches 0, all threads waiting in
 * sys_waitgroup_wait() will be released.
 */
bool sys_waitgroup_done(sys_waitgroup_t *wg);

/**
 * @brief Wait for a wait group to complete
 * @ingroup SystemSyncWaitgroup
 * @param wg Pointer to the wait group to wait on
 *
 * Blocks until the wait group counter reaches 0, then returns. The counter
 * should reach 0 through sys_waitgroup_done() calls from worker threads.
 * Safe to call from multiple threads on the same wait group; all of them
 * are released once the counter reaches 0. Does not deinitialize the wait
 * group - call sys_waitgroup_deinit() once all waiters have returned.
 */
void sys_waitgroup_wait(sys_waitgroup_t *wg);

/** @} */

#ifdef __cplusplus
}
#endif

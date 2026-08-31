/**
 * @file sys/event.h
 * @brief Defines opaque event queues for producer/consumer coordination.
 * @defgroup SystemEvents Events
 * @ingroup System
 * @details
 * The Events module provides the core event transport abstraction used by
 * runloops and other producer/consumer workflows. Events are represented as
 * opaque pointers (`sys_event_t`) so callers can transport arbitrary payload
 * types without imposing a specific object model.
 *
 * Event ownership and payload lifetime are defined by the producer/consumer
 * contract in each subsystem. The queue itself only stores and forwards
 * pointers.
 *
 * `NULL` is reserved as a sentinel return value for empty/timeout/shutdown
 * conditions, so valid posted events must always be non-NULL.
 */

/**
 * @defgroup SystemEventQueue Queue
 * @ingroup SystemEvents
 * @details
 * Event queues provide thread-safe FIFO coordination between producers and
 * consumers.
 *
 * Queue behavior summary:
 * - `sys_event_queue_push()` guarantees insertion and may overwrite the oldest
 *   item when full.
 * - `sys_event_queue_try_push()` never overwrites and fails when full.
 * - `sys_event_queue_pop()` blocks until an event is available or shutdown is
 *   observed.
 * - `sys_event_queue_timed_pop()` supports bounded waiting.
 * - `sys_event_queue_shutdown()` prevents future pushes and wakes blocked
 *   consumers.
 *
 * Typical flow:
 * 1. Allocate with `sys_event_queue_init(capacity)`.
 * 2. Push events from producer threads/interrupt-safe contexts as supported.
 * 3. Pop events in one or more consumer workers.
 * 4. Call `sys_event_queue_shutdown()` to stop intake and unblock waiters.
 * 5. Deinitialize with `sys_event_queue_deinit()`.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Event payload stored in a queue.
 * @ingroup SystemEventQueue
 *
 * `NULL` is reserved to report an empty queue, timeout, or shutdown, so pushed
 * events must be non-NULL.
 */
typedef void *sys_event_t;

/**
 * @brief Opaque event queue.
 * @ingroup SystemEventQueue
 * @headerfile event.h picofuse/sys.h
 */
typedef struct sys_event_queue_t sys_event_queue_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{
 */

/**
 * @brief Allocate a new event queue.
 * @ingroup SystemEventQueue
 * @param capacity Maximum number of events retained by the queue.
 * @return New queue, or `NULL` on allocation or initialization failure.
 */
sys_event_queue_t *sys_event_queue_init(size_t capacity);

/**
 * @brief Deinitialize an event queue and release its resources.
 * @ingroup SystemEventQueue
 * @param queue Queue to deinitialize.
 */
void sys_event_queue_deinit(sys_event_queue_t *queue);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{
 */

/**
 * @brief Push an event, overwriting the oldest entry if the queue is full.
 * @ingroup SystemEventQueue
 * @param queue Queue to write to.
 * @param event Non-NULL event payload.
 * @return `true` on success, `false` on error or after shutdown.
 */
bool sys_event_queue_push(sys_event_queue_t *queue, sys_event_t event);

/**
 * @brief Try to push an event without overwriting.
 * @ingroup SystemEventQueue
 * @param queue Queue to write to.
 * @param event Non-NULL event payload.
 * @return `true` on success, `false` if full, invalid, or shut down.
 */
bool sys_event_queue_try_push(sys_event_queue_t *queue, sys_event_t event);

/**
 * @brief Peek at the next queued event without removing it.
 * @ingroup SystemEventQueue
 * @param queue Queue to inspect.
 * @return Next event, or `NULL` if none is available.
 *
 * The caller is responsible for holding the queue lock while peeking.
 */
sys_event_t sys_event_queue_peek(sys_event_queue_t *queue);

/**
 * @brief Pop the next event, blocking until one is available or shutdown.
 * @ingroup SystemEventQueue
 * @param queue Queue to read from.
 * @return Next event, or `NULL` if the queue is shut down and drained.
 */
sys_event_t sys_event_queue_pop(sys_event_queue_t *queue);

/**
 * @brief Pop the next event without blocking.
 * @ingroup SystemEventQueue
 * @param queue Queue to read from.
 * @return Next event, or `NULL` if empty or invalid.
 */
sys_event_t sys_event_queue_try_pop(sys_event_queue_t *queue);

/**
 * @brief Pop the next event with a timeout.
 * @ingroup SystemEventQueue
 * @param queue Queue to read from.
 * @param timeout_ms Timeout in milliseconds. `0` waits indefinitely.
 * @return Next event, or `NULL` on timeout, invalid queue, or shutdown.
 */
sys_event_t sys_event_queue_timed_pop(sys_event_queue_t *queue,
                                      uint32_t timeout_ms);

/**
 * @brief Return the current event count.
 * @ingroup SystemEventQueue
 * @param queue Queue to inspect.
 * @return Snapshot of the current number of queued events.
 */
size_t sys_event_queue_size(sys_event_queue_t *queue);

/**
 * @brief Return the configured maximum queue capacity.
 * @ingroup SystemEventQueue
 * @param queue Queue to inspect.
 * @return Maximum event capacity, or `0` when queue is invalid.
 */
size_t sys_event_queue_capacity(sys_event_queue_t *queue);

/**
 * @brief Report whether a queue is empty.
 * @ingroup SystemEventQueue
 * @param queue Queue to inspect.
 * @return `true` if the queue is empty, `false` otherwise.
 */
bool sys_event_queue_empty(sys_event_queue_t *queue);

/**
 * @brief Prevent future pushes and wake blocked consumers.
 * @ingroup SystemEventQueue
 * @param queue Queue to shut down.
 */
void sys_event_queue_shutdown(sys_event_queue_t *queue);

/**
 * @brief Lock a queue for manual inspection.
 * @ingroup SystemEventQueue
 * @param queue Queue to lock.
 * @return `true` on success, `false` on error.
 *
 * Thread-context use only (this may block); never call from an IRQ handler.
 * On backends where pushes are IRQ-safe, this does not exclude a concurrent
 * push/pop, only other sys_event_queue_lock() callers - it coordinates
 * callers of this function with each other, not with the queue's own
 * internal operations.
 */
bool sys_event_queue_lock(sys_event_queue_t *queue);

/**
 * @brief Unlock a queue after manual inspection.
 * @ingroup SystemEventQueue
 * @param queue Queue to unlock.
 * @return `true` on success, `false` on error.
 */
bool sys_event_queue_unlock(sys_event_queue_t *queue);

/** @} */

#ifdef __cplusplus
}
#endif

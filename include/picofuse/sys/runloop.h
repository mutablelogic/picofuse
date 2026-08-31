/**
 * @file sys/runloop.h
 * @brief Single process-wide run loop for dispatching events across cores or
 * threads.
 * @defgroup SystemEventRunloop Run Loop
 * @ingroup SystemEvents
 * @details
 * The run loop is a process-wide singleton that drains an event queue across
 * one or more workers.
 *
 * Usage model:
 * - Create an event queue with `sys_event_queue_init()` and start the loop
 *   with `sys_runloop_run()`.
 * - Post events from producer contexts using `sys_runloop_post()`.
 * - Handle events in the callback supplied to `sys_runloop_run()`.
 * - Request shutdown with `sys_runloop_shutdown(exit_value)`.
 *
 * Execution semantics:
 * - The calling thread always acts as worker 0.
 * - Additional workers are created when `num_workers > 1`.
 * - Worker init/exit hooks allow per-worker setup/teardown.
 * - Shutdown stops new intake, drains queued work, and returns the supplied
 *   exit value once workers exit.
 *
 * Call `sys_runloop_run()` from the main thread with an event handler; it
 * blocks until `sys_runloop_shutdown()` is called. Post events from any thread
 * with `sys_runloop_post()` once the loop is running.
 *
 * @code
 *   static void on_event(sys_event_t event) {
 *     // handle event
 *     if (done) {
 *       sys_runloop_shutdown(0);
 *     }
 *   }
 *
 *   sys_event_queue_t *queue = sys_event_queue_init(32);
 *   sys_runloop_run(2, queue, NULL, on_event, NULL, NULL); // blocks; uses 2 workers
 *   sys_event_queue_deinit(queue);
 * @endcode
 *
 * On Pico the calling thread counts as one worker; additional workers are
 * pinned to subsequent cores. On other platforms each additional worker is a
 * new thread.
 */
#pragma once

#include "event.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Per-worker initialisation callback.
 * @ingroup SystemEventRunloop
 * @param worker_index Zero-based index of the worker (0 = calling thread /
 *                     core 0, 1 = first additional worker, etc.).
 *
 * Called once on each worker before it starts draining the event queue. Use
 * this to register IRQ handlers, allocate thread-local resources, or perform
 * any other per-core setup. May be `NULL` if no initialisation is needed.
 */
typedef void (*sys_runloop_init_func_t)(uint8_t worker_index);

/**
 * @brief Event handler called on a worker for each posted event.
 * @ingroup SystemEventRunloop
 * @param event The event posted via sys_runloop_post().
 */
typedef void (*sys_runloop_func_t)(sys_event_t event);

/**
 * @brief Optional periodic poll callback invoked by worker 0.
 * @ingroup SystemEventRunloop
 *
 * This callback is executed once per timed runloop wait iteration before
 * dispatched events are handled. Pass `NULL` when no periodic polling is
 * required.
 */
typedef void (*sys_runloop_poll_func_t)(void);

/**
 * @brief Per-worker exit callback.
 * @ingroup SystemEventRunloop
 * @param worker_index Zero-based index of the worker.
 *
 * Called once on each worker after the event queue is drained and before the
 * worker exits. Use this to deregister IRQ handlers or release thread-local
 * resources. May be `NULL` if no cleanup is needed.
 */
typedef void (*sys_runloop_exit_func_t)(uint8_t worker_index);

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Start the run loop and block until shutdown.
 * @ingroup SystemEventRunloop
 * @param num_workers Total number of workers, including the calling thread.
 *                    Pass 0 to use all available cores. Values greater than
 *                    the number of available cores are clamped to that limit.
 * @param queue Event queue to use for runloop dispatch. Must be valid; its
 *              lifetime remains the caller's responsibility -
 *              sys_runloop_run() neither initializes nor deinitializes it.
 * @param init  Called once per worker before it starts. May be `NULL`.
 * @param callback Handler invoked on a worker for each event dequeued.
 * @param poll_fn Optional periodic poll callback invoked by worker 0.
 *                May be `NULL`.
 * @param exit  Called once per worker after the queue is drained. May be
 * `NULL`.
 * @return Exit value passed to sys_runloop_shutdown() once all workers have
 *         finished draining the queue.
 *
 * The calling thread becomes worker 0. If @p num_workers is greater than 1,
 * additional workers (1, 2, …) are started on other cores or threads, each
 * receiving their index in @p init and @p exit.
 */
uint32_t sys_runloop_run(uint8_t num_workers, sys_event_queue_t *queue,
                         sys_runloop_init_func_t init,
                         sys_runloop_func_t callback,
                         sys_runloop_poll_func_t poll_fn,
                         sys_runloop_exit_func_t exit);

/**
 * @brief Signal the run loop to stop accepting events and exit when drained.
 * @ingroup SystemEventRunloop
 * @param exit_value Value returned by sys_runloop_run() once all workers
 *                   have exited.
 *
 * After this call sys_runloop_post() returns `false`. Workers finish any
 * in-progress and already-queued events, then exit, causing sys_runloop_run()
 * to return @p exit_value. Safe to call from any thread or event handler. Has
 * no effect if the run loop is not running.
 */
void sys_runloop_shutdown(uint32_t exit_value);

/**
 * @brief Post an event to be processed by the run loop.
 * @ingroup SystemEventRunloop
 * @param event Non-NULL event payload.
 * @return `true` on success, `false` if the run loop is shut down or the
 *         queue is full.
 *
 * Safe to call from any thread while the run loop is running. Returns
 * `false` if called before sys_runloop_run() or after sys_runloop_shutdown().
 * Events are processed in the order posted.
 */
bool sys_runloop_post(sys_event_t event);

/**
 * @brief Get the active runloop event queue.
 * @ingroup SystemEventRunloop
 * @return Active queue pointer while runloop is running, else `NULL`.
 */
sys_event_queue_t *sys_runloop_queue(void);

/** @} */

#ifdef __cplusplus
}
#endif

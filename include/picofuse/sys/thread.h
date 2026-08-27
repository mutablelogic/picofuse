/**
 * @file sys/thread.h
 * @brief Defines thread creation, core query, and sleep primitives.
 * @defgroup SystemThread Thread and Sleep Operations
 * @ingroup System
 * @details
 * The SystemThread module provides lightweight cross-platform helpers for
 * creating fire-and-forget worker threads and querying CPU core information.
 *
 * Thread creation APIs (`sys_thread_create`, `sys_thread_create_on_core`)
 * execute a user callback with a single opaque argument and return immediately
 * after the worker is scheduled. Workers terminate when the callback returns.
 *
 * Core-query APIs (`sys_thread_numcores`, `sys_thread_core`) support adaptive
 * scheduling decisions and diagnostics for multicore execution.
 *
 * Typical flow:
 * 1. Query available cores with `sys_thread_numcores()`.
 * 2. Spawn one or more workers with `sys_thread_create*()`.
 * 3. Coordinate shared state with SystemSync primitives (mutex/cond/waitgroup
 *    or atomics).
 * 4. Optionally inspect worker placement with `sys_thread_core()`.
 *
 * Notes:
 * - These APIs are non-joinable by design; completion coordination should be
 *   handled externally (for example using waitgroups).
 * - Static-pool backends may cap concurrent active threads via
 *   `SYS_THREAD_CAPACITY`.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @def SYS_THREAD_CAPACITY
 * @ingroup SystemThread
 * @brief Maximum number of concurrently active thread contexts in static-pool
 * implementations.
 */
#ifndef SYS_THREAD_CAPACITY
#define SYS_THREAD_CAPACITY 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Thread function signature
 * @ingroup SystemThread
 *
 * Function signature for thread entry points. The function receives
 * a single void pointer argument and should not return a value.
 * The thread terminates when this function returns.
 */
typedef void (*sys_thread_func_t)(void *arg);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @name Methods
 * @{ */

/**
 * @brief Returns the number of CPU cores available on the host system.
 * @ingroup SystemThread
 * @return The number of CPU cores available on the system. Returns 1 if the
 *         number of cores cannot be determined or if the system has only one
 *         core.
 *
 * This function queries the system to determine the total number of processing
 * cores available.
 */
uint8_t sys_thread_numcores(void);

/**
 * @brief Create a thread on any available core
 * @ingroup SystemThread
 * @param func Function to execute in the new thread
 * @param arg Argument to pass to the thread function
 * @return true if thread was created successfully, false if no cores available
 * or error
 *
 * Creates a new thread that executes the specified function with the given
 * argument. The thread runs independently and terminates when the function
 * returns. No cleanup or joining is required - the thread is fire-and-forget.
 * On systems with multiple cores, the thread may be scheduled on any available
 * core. Implementations that use a static thread context pool may return
 * false after `SYS_THREAD_CAPACITY` concurrent threads have been created.
 */
bool sys_thread_create(sys_thread_func_t func, void *arg);

/**
 * @brief Create a thread on a specific core
 * @ingroup SystemThread
 * @param func Function to execute in the new thread
 * @param arg Argument to pass to the thread function
 * @param core Core number to run the thread on (0-based)
 * @return true if thread was created on the specified core, false if core
 * unavailable or error
 *
 * Creates a new thread that executes the specified function on a specific CPU
 * core. The thread runs independently and terminates when the function returns.
 * If the specified core is not available or already in use, the function
 * returns false. Core 0 is typically the main/boot core. Implementations that
 * use a static thread context pool may return false after
 * `SYS_THREAD_CAPACITY` concurrent threads have been created.
 */
bool sys_thread_create_on_core(sys_thread_func_t func, void *arg, uint8_t core);

/**
 * @brief Get the CPU core number the current thread is running on
 * @ingroup SystemThread
 * @return The core number (0-based) that the current thread is executing on.
 *         Returns 0 if the core cannot be determined or on single-core systems.
 *
 * This function queries the system to determine which CPU core the calling
 * thread is currently scheduled on. Note that threads may migrate between
 * cores, so this value may change over time unless thread affinity is set.
 */
uint8_t sys_thread_core(void);

/** @} */

#ifdef __cplusplus
}
#endif
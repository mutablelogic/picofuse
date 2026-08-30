/**
 * @file sys/env.h
 * @brief Environment information.
 * @defgroup SystemEnv Environment
 * @ingroup Execution
 * @details
 * The Environment module provides runtime metadata about the active execution
 * context and a lightweight interface for handling process-level environment
 * signals.
 *
 * Metadata methods return stable identifiers that help applications report or
 * route behavior per environment, including:
 * - `sys_env_serial()` for unique identity information when available.
 * - `sys_env_name()` for process/program naming.
 * - `sys_env_system()` for platform identification.
 * - `sys_env_version()` for runtime version reporting.
 *
 * Signal integration is exposed through `sys_env_signalhandler()`, which
 * registers a callback for termination-related signals. This is useful for
 * graceful shutdown, cancellation, and runloop exit coordination.
 *
 * Signal handling notes:
 * - Only one callback registration is active at a time.
 * - Signal support may vary by platform.
 * - Callbacks may run in constrained contexts; keep handlers minimal and
 *   non-blocking.
 *
 * Typical flow:
 * 1. Register a signal callback during startup (if needed).
 * 2. Read environment metadata for diagnostics/logging/capability decisions.
 * 3. On shutdown, clear signal callbacks or let deinit paths unregister.
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Environment signal types.
 * @ingroup SystemEnv
 *
 * Enumeration of signal types that can be received from the environment or
 * operating system. These signals typically indicate termination or interrupt
 * requests that applications should handle gracefully.
 */
typedef enum {
  SYS_ENV_SIGNAL_NONE = 0,  ///< No signal.
  SYS_ENV_SIGNAL_TERM = 1u, ///< Termination request from the environment.
  SYS_ENV_SIGNAL_INT = 2u,  ///< Interrupt request from the environment.
  SYS_ENV_SIGNAL_QUIT = 4u, ///< Quit request from the environment.
} sys_env_signal_t;

/**
 * @brief Callback function type for handling environment signals.
 * @ingroup SystemEnv
 * @param signal The type of signal that was received.
 */
typedef void (*sys_env_signal_callback_t)(sys_env_signal_t signal);

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{
 */

/**
 * @brief Set a handler for environment signals.
 * @ingroup SystemEnv
 * @param mask Bitmask of `sys_env_signal_t` values to handle, or zero to
 * handle all supported signals.
 * @param callback Callback to invoke when a signal is received, or `NULL` to
 * disable signal handling.
 * @return `true` when the handler was updated successfully, `false` when
 * signal handling is not supported on the current platform.
 *
 * Only one signal handler can be active at a time. Setting a new handler
 * replaces any previously registered handler.
 *
 * Not all platforms support all signal types. Embedded platforms may have
 * limited or no signal support.
 *
 * The signal handler may be called from interrupt context on some platforms.
 * Keep the callback simple and avoid blocking operations, memory allocation,
 * or complex system calls.
 */
bool sys_env_signalhandler(sys_env_signal_t mask,
                           sys_env_signal_callback_t callback);

/**
 * @brief Return a unique identifier for the current environment.
 * @ingroup SystemEnv
 * @return A serial number or other unique identifier as a string.
 */
const char *sys_env_serial(void);

/**
 * @brief Return the name of the current environment.
 * @ingroup SystemEnv
 * @return The name of the running program or environment.
 */
const char *sys_env_name(void);

/**
 * @brief Return the system identifier for the current environment.
 * @ingroup SystemEnv
 * @return A system identifier string such as "linux", "darwin", or a Pico
 * board name when running on Pico targets.
 */
const char *sys_env_system(void);

/**
 * @brief Return the version of the current environment.
 * @ingroup SystemEnv
 * @return The version of the running program or environment.
 */
const char *sys_env_version(void);

/** @} */

#ifdef __cplusplus
}
#endif

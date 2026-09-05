/**
 * @file sys/env.h
 * @brief Environment information.
 * @defgroup SystemEnv Environment
 * @ingroup Execution
 * @brief Runtime metadata about the active execution context, plus
 * process-level environment signal handling.
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
#include "io.h"
#include <stdbool.h>
#include <stddef.h>
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
  sys_env_signal_none = 0,  ///< No signal.
  sys_env_signal_term = 1u, ///< Termination request from the environment.
  sys_env_signal_int = 2u,  ///< Interrupt request from the environment.
  sys_env_signal_quit = 4u, ///< Quit request from the environment.
} sys_env_signal_t;

/**
 * @brief Callback function type for handling environment signals.
 * @ingroup SystemEnv
 * @param signal The type of signal that was received.
 */
typedef void (*sys_env_signal_callback_t)(sys_env_signal_t signal);

/**
 * @brief Argument flag value types.
 * @ingroup SystemEnv
 *
 * Identifies which kind of value a `sys_env_arg_flag_t` expects. Used by
 * `sys_env_arg_parse()` to validate a matched argument's value.
 */
typedef enum {
  sys_env_arg_type_bool = 1,   ///< Argument flag expects a boolean value.
  sys_env_arg_type_string = 2, ///< Argument flag expects a string value.
  sys_env_arg_type_int = 3,    ///< Argument flag expects an integer value.
  sys_env_arg_type_uint = 4,   ///< Argument flag expects an unsigned integer value.
  sys_env_arg_type_float = 5,  ///< Argument flag expects a floating-point value.
} sys_env_arg_type_t;

/**
 * @brief Describes a single command-line argument flag.
 * @ingroup SystemEnv
 * @headerfile env.h picofuse/sys.h
 *
 * On input to `sys_env_arg_parse()`, `long_name`/`short_name`/`type`
 * describe the flag to look for, and `value` holds its default. On return,
 * `value` holds the matched argument's value if the flag was present on
 * the command line, or is left untouched at its default otherwise.
 */
typedef struct sys_env_arg_flag_t {
  const char *long_name;   ///< Name of the argument flag.
  const char *short_name;  ///< Short name or single-character alias for the
                           ///< argument flag.
  sys_env_arg_type_t type; ///< Expected value type for the argument flag.
  const char *value;       ///< Default value, or the matched value after a
                           ///< successful parse.
} sys_env_arg_flag_t;

/**
 * @def SYS_ENV_ARG_CAPACITY
 * @ingroup SystemEnv
 * Maximum number of positional (non-flag) command-line arguments
 * `sys_env_arg_parse()` can record.
 */
#ifndef SYS_ENV_ARG_CAPACITY
#define SYS_ENV_ARG_CAPACITY 16
#endif

/**
 * @brief The result of a successful sys_env_arg_parse() call.
 * @ingroup SystemEnv
 * @headerfile env.h picofuse/sys.h
 *
 * A single, process-wide instance - there is only ever one process command
 * line to parse, so this is never pool-allocated. Returned by
 * sys_env_arg_parse() and passed back into the `sys_env_arg_*()` accessors
 * that read positional arguments or matched flag values.
 */
typedef struct sys_env_arg_t sys_env_arg_t;

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

/**
 * @brief Replace the argc/argv that sys_env_arg_parse() reads.
 * @ingroup SystemEnv
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * sys_init() already calls this once with the process's real argc/argv -
 * most programs never need to call it themselves. It exists for the rare
 * case of validating sys_env_arg_parse() against several different
 * command lines in one process (e.g. a test), without re-running
 * sys_init()'s other, one-time module setup (stdio, the default arena,
 * ...) again for each one.
 */
void sys_env_set_args(int argc, char *argv[]);

/**
 * @brief Parse the process's command-line arguments against a set of flags.
 * @ingroup SystemEnv
 * @param flags Array of flag descriptors to match against. Use a
 * zero-initialized `sys_env_arg_flag_t` (`long_name == NULL`) to mark the
 * end of the array - there is no separate count parameter.
 * @return A pointer to the parse result on success or `NULL` on a malformed or
 * unrecognized argument.
 *
 * Reads from the `argc`/`argv` captured by `sys_init()`. Non-flag argv
 * entries are collected as positional arguments, in order. Flags are matched
 * against the provided descriptors, either by long name (`--flag`) or short
 * name (`-f`).
 *
 * Boolean flags are treated as `true` if present, or `--no-\<flag\>` to
 * explicitly set them to `false`. If not present, the default value is used,
 * or 'false' if no default is specified.
 *
 * When a flag contains a "=" character, the portion after the "=" is treated
 * as the flag's value, else the next argv entry is treated as the value. The
 * argument
 * '-' by itself is skipped and indicates the remaining arguments are
 * positional.
 */
sys_env_arg_t *sys_env_arg_parse(sys_env_arg_flag_t *flags);

/**
 * @brief Return the number of positional arguments.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @return The number of positional arguments.
 */
size_t sys_env_arg_count(sys_env_arg_t *args);

/**
 * @brief Return the value of a positional argument by index.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param index The index of the positional argument.
 * @return The value of the positional argument, or `NULL` if the index is out
 * of bounds.
 */
const char *sys_env_arg_string(sys_env_arg_t *args, size_t index);

/**
 * @brief Print the usage information for the command-line flags.
 * @ingroup SystemEnv
 * @param flags Array of flag descriptors to display usage for, terminated
 * the same way as `sys_env_arg_parse()`'s own `flags` parameter.
 * @param stream The stream to print the usage information to.
 * @return true if the usage information was successfully printed, false
 * otherwise.
 */
bool sys_env_arg_usage(sys_env_arg_flag_t *flags, sys_iostream_t *stream);

/**
 * @brief Look up a parsed flag's value as a boolean.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a boolean variable where the result will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_bool(sys_env_arg_t *args, const char *name, bool *value);

/**
 * @brief Look up a parsed flag's value as a 32-bit signed integer.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a 32-bit signed integer variable where the result
 * will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_int32(sys_env_arg_t *args, const char *name,
                             int32_t *value);

/**
 * @brief Look up a parsed flag's value as a 32-bit unsigned integer.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a 32-bit unsigned integer variable where the result
 * will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_uint32(sys_env_arg_t *args, const char *name,
                              uint32_t *value);

/**
 * @brief Look up a parsed flag's value as a 64-bit signed integer.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a 64-bit signed integer variable where the result
 * will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_int64(sys_env_arg_t *args, const char *name,
                             int64_t *value);

/**
 * @brief Look up a parsed flag's value as a 64-bit unsigned integer.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a 64-bit unsigned integer variable where the result
 * will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_uint64(sys_env_arg_t *args, const char *name,
                              uint64_t *value);

/**
 * @brief Look up a parsed flag's value as a 32-bit single-precision
 * floating-point number.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a 32-bit single-precision floating-point variable
 * where the result will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_float32(sys_env_arg_t *args, const char *name,
                               float *value);

/**
 * @brief Look up a parsed flag's value as a 64-bit double-precision
 * floating-point number.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value Pointer to a 64-bit double-precision floating-point variable
 * where the result will be stored.
 * @return true if the flag was found and successfully parsed, or false if the
 * flag wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
bool sys_env_arg_parse_float64(sys_env_arg_t *args, const char *name,
                               double *value);

/**
 * @brief Look up a parsed flag's value as a string.
 * @ingroup SystemEnv
 * @param args The parse result returned by `sys_env_arg_parse()`.
 * @param name The flag's long or short name.
 * @param value A buffer to store the flag's value. If NULL only the required
 * buffer size is returned.
 * @param cap The capacity of the buffer.
 * @return The number of bytes written to the buffer, or `0` if the flag
 * wasn't found or `sys_env_arg_parse()` hasn't succeeded yet.
 */
size_t sys_env_arg_parse_string(sys_env_arg_t *args, const char *name,
                                char *value, size_t cap);

/** @} */

#ifdef __cplusplus
}
#endif

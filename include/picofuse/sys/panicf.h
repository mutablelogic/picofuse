/**
 * @file sys/panicf.h
 * @ingroup System
 * @brief Defines the `sys_panicf` function for formatted panic messages.
 * @details This file provides the declaration of the `sys_panicf` function,
 * which can be used to print formatted panic messages and halt the system.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Prints a formatted panic message and halts the system.
 * @param format A printf-style format string describing the panic.
 * @param ... Additional arguments referenced by the format string.
 * @details This function emits a formatted fatal error message using the
 * platform-specific panic path and does not return to the caller.
 */
_Noreturn void sys_panicf(const char *format, ...);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

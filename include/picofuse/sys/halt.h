/**
 * @file sys/halt.h
 * @ingroup System
 * @brief Defines the `sys_halt` function.
 * @details This file provides a platform-specific primitive that stops normal
 * execution and never returns.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Halts the system and never returns.
 * @ingroup System
 * @details This function stops normal program execution using the
 * platform-specific halt mechanism. Callers should treat this as terminal
 * control flow.
 */
_Noreturn void sys_halt(void);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

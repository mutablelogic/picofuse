/**
 * @file sys/sleep.h
 * @brief Defines thread sleep primitives.
 * @defgroup SystemSleepHalt Sleep and Halt
 * @ingroup Execution
 * @brief Pauses execution for a specified duration, or stops it permanently.
 * @details This file declares functions that pause execution for a specified
 * duration; sys/halt.h declares the counterpart that stops it permanently.
 */

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Pauses the current thread for a specified duration.
 * @ingroup SystemSleepHalt
 * @param ms The number of milliseconds to sleep.
 * @details This function blocks only the calling thread for approximately the
 * requested duration.
 */
void sys_sleep_ms(uint32_t ms);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

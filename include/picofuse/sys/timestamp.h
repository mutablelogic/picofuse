/**
 * @file sys/timestamp.h
 * @brief Defines timestamp APIs.
 * @defgroup SystemTime Date and Time Operations
 * @ingroup System
 * @details
 * The SystemTime module provides monotonic timing and wall-clock date/time
 * operations for scheduling, time measurement, and timestamped application
 * logic.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Gets the number of milliseconds since the process launched.
 * @ingroup SystemTime
 */
uint64_t sys_timestamp_ms(void);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

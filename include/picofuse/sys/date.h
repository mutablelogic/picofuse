/**
 * @file sys/date.h
 * @brief Defines date and wall-clock time APIs.
 * @ingroup SystemTime
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
 * @brief Represents a system date and time.
 * @ingroup SystemTime
 * @headerfile date.h picofuse/sys.h
 */
typedef struct sys_date_t {
  int64_t seconds;     ///< Seconds since the Unix epoch in UTC.
  int32_t nanoseconds; ///< Fractional seconds in nanoseconds.
  int32_t tzoffset;    ///< Timezone offset in seconds east of UTC.
} sys_date_t;

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{
 */

/**
 * @brief Get the current system date and time.
 * @ingroup SystemTime
 * @param date Structure to populate with the current time and timezone.
 * @return `true` on success, `false` on error.
 */
bool sys_date_get_now(sys_date_t *date);

/**
 * @brief Set the current system date and time.
 * @ingroup SystemTime
 * @param date Structure containing the new time and timezone offset.
 * @return `true` on success, `false` on error.
 */
bool sys_date_set_now(const sys_date_t *date);

/**
 * @brief Extract UTC time components from a date.
 * @ingroup SystemTime
 * @param date Date to extract from, or `NULL` to use the current system time.
 * @param hours Receives hours in the range 0-23 when non-NULL.
 * @param minutes Receives minutes in the range 0-59 when non-NULL.
 * @param seconds Receives seconds in the range 0-59 when non-NULL.
 * @return `true` on success, `false` on error.
 */
bool sys_date_get_time_utc(const sys_date_t *date, uint8_t *hours,
                            uint8_t *minutes, uint8_t *seconds);

/**
 * @brief Extract local time components from a date.
 * @ingroup SystemTime
 * @param date Date to extract from, or `NULL` to use the current system time.
 * @param hours Receives hours in the range 0-23 when non-NULL.
 * @param minutes Receives minutes in the range 0-59 when non-NULL.
 * @param seconds Receives seconds in the range 0-59 when non-NULL.
 * @return `true` on success, `false` on error.
 */
bool sys_date_get_time_local(const sys_date_t *date, uint8_t *hours,
                              uint8_t *minutes, uint8_t *seconds);

/**
 * @brief Extract UTC date components from a date.
 * @ingroup SystemTime
 * @param date Date to extract from, or `NULL` to use the current system time.
 * @param year Receives the full year when non-NULL.
 * @param month Receives the month in the range 1-12 when non-NULL.
 * @param day Receives the day of month in the range 1-31 when non-NULL.
 * @param weekday Receives the weekday in the range 0-6 when non-NULL.
 * @return `true` on success, `false` on error.
 */
bool sys_date_get_date_utc(const sys_date_t *date, uint16_t *year,
                            uint8_t *month, uint8_t *day, uint8_t *weekday);

/**
 * @brief Extract local date components from a date.
 * @ingroup SystemTime
 * @param date Date to extract from, or `NULL` to use the current system time.
 * @param year Receives the full year when non-NULL.
 * @param month Receives the month in the range 1-12 when non-NULL.
 * @param day Receives the day of month in the range 1-31 when non-NULL.
 * @param weekday Receives the weekday in the range 0-6 when non-NULL.
 * @return `true` on success, `false` on error.
 */
bool sys_date_get_date_local(const sys_date_t *date, uint16_t *year,
                              uint8_t *month, uint8_t *day, uint8_t *weekday);

/**
 * @brief Set UTC time components on an existing date.
 * @ingroup SystemTime
 * @param date Date structure to modify.
 * @param hours Hours in the range 0-23.
 * @param minutes Minutes in the range 0-59.
 * @param seconds Seconds in the range 0-59.
 * @return `true` on success, `false` on invalid parameters.
 */
bool sys_date_set_time_utc(sys_date_t *date, uint8_t hours, uint8_t minutes,
                            uint8_t seconds);

/**
 * @brief Set UTC date components on an existing date.
 * @ingroup SystemTime
 * @param date Date structure to modify.
 * @param year Full year value.
 * @param month Month in the range 1-12.
 * @param day Day of month in the range 1-31.
 * @return `true` on success, `false` on invalid parameters.
 */
bool sys_date_set_date_utc(sys_date_t *date, uint16_t year, uint8_t month,
                            uint8_t day);

/**
 * @brief Compare two dates in nanoseconds.
 * @ingroup SystemTime
 * @param start Start time, or `NULL` to use the current system time.
 * @param end End time, or `NULL` to return 0.
 * @return Signed nanosecond difference between `start` and `end`, or 0 when
 * `end` is `NULL`.
 */
int64_t sys_date_compare_ns(const sys_date_t *start, const sys_date_t *end);

/** @} */

#ifdef __cplusplus
}
#endif

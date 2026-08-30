#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS

/**
 * @brief Return whether a year is a leap year in the Gregorian calendar.
 * @param year Full year value.
 * @return `true` when the year is a leap year.
 */
static inline bool _sys_date_is_leap_year(uint16_t year) {
  return (year % 4u == 0u && year % 100u != 0u) || (year % 400u == 0u);
}

/**
 * @brief Return the number of days in a month.
 * @param year Full year value.
 * @param month Month in the range 1-12.
 * @return Days in the month, or 0 for invalid months.
 */
static inline uint8_t _sys_date_days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t days_per_month[] = {31, 28, 31, 30, 31, 30,
                                           31, 31, 30, 31, 30, 31};

  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && _sys_date_is_leap_year(year)) {
    return 29;
  }

  return days_per_month[month - 1u];
}

/**
 * @brief Convert a Gregorian UTC calendar date into days since the Unix epoch.
 * @param year Full year value.
 * @param month Month in the range 1-12.
 * @param day Day of month in the range 1-31.
 * @return Days since 1970-01-01.
 */
static inline int64_t _sys_date_days_from_civil(uint16_t year, uint8_t month,
                                                uint8_t day) {
  int64_t y = (int64_t)year;
  int64_t m = (int64_t)month;
  int64_t d = (int64_t)day;

  y -= m <= 2 ? 1 : 0;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  uint64_t yoe = (uint64_t)(y - era * 400);
  uint64_t doy = (uint64_t)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  uint64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

/**
 * @brief Convert UTC calendar components into seconds since the Unix epoch.
 * @param year Full year value.
 * @param month Month in the range 1-12.
 * @param day Day of month in the range 1-31.
 * @param hours Hours in the range 0-23.
 * @param minutes Minutes in the range 0-59.
 * @param seconds Seconds in the range 0-59.
 * @param result Receives the converted seconds on success.
 * @return `true` on success, `false` on invalid inputs.
 */
static bool _sys_date_make_utc_seconds(uint16_t year, uint8_t month,
                                       uint8_t day, uint8_t hours,
                                       uint8_t minutes, uint8_t seconds,
                                       int64_t *result) {
  if (result == NULL || month < 1 || month > 12 || hours >= 24 ||
      minutes >= 60 || seconds >= 60) {
    return false;
  }

  uint8_t max_day = _sys_date_days_in_month(year, month);
  if (max_day == 0 || day < 1 || day > max_day) {
    return false;
  }

  int64_t days = _sys_date_days_from_civil(year, month, day);
  *result = days * 86400 + (int64_t)hours * 3600 + (int64_t)minutes * 60 +
            (int64_t)seconds;
  return true;
}

/**
 * @brief Convert days since the Unix epoch into a Gregorian UTC calendar
 * date - the inverse of _sys_date_days_from_civil().
 * @param days Days since 1970-01-01.
 * @param year Optional output for the full year.
 * @param month Optional output for the month, in the range 1-12.
 * @param day Optional output for the day of month, in the range 1-31.
 */
static inline void _sys_date_civil_from_days(int64_t days, uint16_t *year,
                                             uint8_t *month, uint8_t *day) {
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097; // [0, 146096]
  int64_t yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
  int64_t mp = (5 * doy + 2) / 153;                      // [0, 11]
  int64_t d = doy - (153 * mp + 2) / 5 + 1;              // [1, 31]
  int64_t m = mp + (mp < 10 ? 3 : -9);                   // [1, 12]

  if (year != NULL) {
    *year = (uint16_t)(y + (m <= 2 ? 1 : 0));
  }
  if (month != NULL) {
    *month = (uint8_t)m;
  }
  if (day != NULL) {
    *day = (uint8_t)d;
  }
}

/**
 * @brief Return the weekday for a day count since the Unix epoch.
 * @param days Days since 1970-01-01.
 * @return Weekday in the range 0-6, with 0 meaning Sunday (1970-01-01 was a
 * Thursday).
 */
static inline uint8_t _sys_date_weekday_from_days(int64_t days) {
  int64_t floor_mod7 = ((days % 7) + 7) % 7;
  return (uint8_t)((floor_mod7 + 4) % 7);
}

/**
 * @brief Extract date and time components from a UTC second count.
 * @param time_sec Time value in seconds since the Unix epoch.
 * @param hours Optional output for hours.
 * @param minutes Optional output for minutes.
 * @param seconds Optional output for seconds.
 * @param year Optional output for the year.
 * @param month Optional output for the month.
 * @param day Optional output for the day of month.
 * @param weekday Optional output for the weekday.
 * @return `true` on success, `false` when the conversion fails.
 */
static bool _sys_date_extract(int64_t time_sec, uint8_t *hours,
                              uint8_t *minutes, uint8_t *seconds,
                              uint16_t *year, uint8_t *month, uint8_t *day,
                              uint8_t *weekday) {
  // Floor-divide/modulo so that dates before 1970-01-01 (negative
  // time_sec) split into (days, time-of-day) correctly - C's / and %
  // truncate toward zero, which would misplace the day boundary for
  // negative values.
  int64_t days = time_sec / 86400;
  int64_t secs_of_day = time_sec % 86400;
  if (secs_of_day < 0) {
    secs_of_day += 86400;
    days -= 1;
  }

  if (hours != NULL) {
    *hours = (uint8_t)(secs_of_day / 3600);
  }
  if (minutes != NULL) {
    *minutes = (uint8_t)((secs_of_day / 60) % 60);
  }
  if (seconds != NULL) {
    *seconds = (uint8_t)(secs_of_day % 60);
  }
  if (weekday != NULL) {
    *weekday = _sys_date_weekday_from_days(days);
  }
  if (year != NULL || month != NULL || day != NULL) {
    _sys_date_civil_from_days(days, year, month, day);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

bool sys_date_get_time_utc(const sys_date_t *date, uint8_t *hours,
                           uint8_t *minutes, uint8_t *seconds) {
  sys_date_t now;
  if (date == NULL) {
    if (!sys_date_get_now(&now)) {
      return false;
    }
    date = &now;
  }

  return _sys_date_extract(date->seconds, hours, minutes, seconds, NULL, NULL,
                           NULL, NULL);
}

bool sys_date_get_time_local(const sys_date_t *date, uint8_t *hours,
                             uint8_t *minutes, uint8_t *seconds) {
  sys_date_t now;
  if (date == NULL) {
    if (!sys_date_get_now(&now)) {
      return false;
    }
    date = &now;
  }

  return _sys_date_extract(date->seconds + date->tzoffset, hours, minutes,
                           seconds, NULL, NULL, NULL, NULL);
}

bool sys_date_get_date_utc(const sys_date_t *date, uint16_t *year,
                           uint8_t *month, uint8_t *day, uint8_t *weekday) {
  sys_date_t now;
  if (date == NULL) {
    if (!sys_date_get_now(&now)) {
      return false;
    }
    date = &now;
  }

  return _sys_date_extract(date->seconds, NULL, NULL, NULL, year, month, day,
                           weekday);
}

bool sys_date_get_date_local(const sys_date_t *date, uint16_t *year,
                             uint8_t *month, uint8_t *day, uint8_t *weekday) {
  sys_date_t now;
  if (date == NULL) {
    if (!sys_date_get_now(&now)) {
      return false;
    }
    date = &now;
  }

  return _sys_date_extract(date->seconds + date->tzoffset, NULL, NULL, NULL,
                           year, month, day, weekday);
}

bool sys_date_set_time_utc(sys_date_t *date, uint8_t hours, uint8_t minutes,
                           uint8_t seconds) {
  if (date == NULL) {
    return false;
  }
  if (hours >= 24 || minutes >= 60 || seconds >= 60) {
    return false;
  }

  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  if (!_sys_date_extract(date->seconds, NULL, NULL, NULL, &year, &month, &day,
                         NULL)) {
    return false;
  }

  int64_t new_time = 0;
  if (!_sys_date_make_utc_seconds(year, month, day, hours, minutes, seconds,
                                  &new_time)) {
    return false;
  }

  date->seconds = new_time;
  return true;
}

bool sys_date_set_date_utc(sys_date_t *date, uint16_t year, uint8_t month,
                           uint8_t day) {
  if (date == NULL) {
    return false;
  }
  if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) {
    return false;
  }

  uint8_t hours = 0;
  uint8_t minutes = 0;
  uint8_t seconds = 0;
  if (!_sys_date_extract(date->seconds, &hours, &minutes, &seconds, NULL, NULL,
                         NULL, NULL)) {
    return false;
  }

  int64_t new_time = 0;
  if (!_sys_date_make_utc_seconds(year, month, day, hours, minutes, seconds,
                                  &new_time)) {
    return false;
  }

  date->seconds = new_time;
  return true;
}

int64_t sys_date_compare_ns(const sys_date_t *start, const sys_date_t *end) {
  sys_date_t now;
  if (end == NULL) {
    return 0;
  }
  if (start == NULL) {
    if (!sys_date_get_now(&now)) {
      return 0;
    }
    start = &now;
  }

  int64_t sec_diff = end->seconds - start->seconds;
  int64_t ns_diff = sec_diff * 1000000000LL;
  ns_diff += (int64_t)end->nanoseconds - (int64_t)start->nanoseconds;
  return ns_diff;
}

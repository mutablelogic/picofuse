#include "sync.h"
#include <pico/time.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Microseconds from the boot clock to the date, which is the boot clock's
// own resolution
static int64_t _sys_date_offset_us = 0;
static int32_t _sys_date_tzoffset_seconds = 0;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/**
 * @brief Return microseconds since boot.
 * @return Microseconds since boot.
 */
static int64_t _sys_date_now_us(void) {
  absolute_time_t now = get_absolute_time();
  return (int64_t)to_us_since_boot(now);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

bool sys_date_get_now(sys_date_t *date) {
  if (date == NULL) {
    return false;
  }

  // Shared with the mutex/cond/waitgroup/hash pools - held only for a
  // fast timer read plus integer arithmetic, never anything unbounded.
  _sys_sync_pool_lock();

  int64_t us;
  bool ok = !__builtin_add_overflow(_sys_date_now_us(), _sys_date_offset_us,
                                    &us);
  if (ok) {
    // Floor-divide/modulo so a negative us (e.g. shortly after setting a
    // pre-1970 date) still splits into a well-formed (seconds,
    // nanoseconds) pair - C's / and % truncate toward zero, which would
    // otherwise yield negative nanoseconds.
    int64_t seconds = us / 1000000;
    int64_t rem_us = us % 1000000;
    if (rem_us < 0) {
      rem_us += 1000000;
      seconds -= 1;
    }
    date->seconds = seconds;
    date->nanoseconds = (int32_t)(rem_us * 1000);
    date->tzoffset = _sys_date_tzoffset_seconds;
  }

  _sys_sync_pool_unlock();
  return ok;
}

bool sys_date_set_now(const sys_date_t *date) {
  if (date == NULL || date->nanoseconds < 0 ||
      date->nanoseconds >= 1000000000) {
    return false;
  }

  // The date in microseconds, which must fit, as must the offset to it
  // from the boot clock. For a date before 1970, the fraction is taken
  // from the next second up, so that both have the same sign and a date
  // just inside the lower limit isn't refused when its seconds alone
  // wouldn't fit
  int64_t seconds = date->seconds;
  int64_t fraction_us = date->nanoseconds / 1000;
  if (seconds < 0 && fraction_us > 0) {
    seconds += 1;
    fraction_us -= 1000000;
  }
  int64_t desired_us;
  if (__builtin_mul_overflow(seconds, (int64_t)1000000, &desired_us) ||
      __builtin_add_overflow(desired_us, fraction_us, &desired_us)) {
    return false;
  }

  _sys_sync_pool_lock();

  int64_t offset_us;
  bool ok = !__builtin_sub_overflow(desired_us, _sys_date_now_us(), &offset_us);
  if (ok) {
    _sys_date_offset_us = offset_us;
    _sys_date_tzoffset_seconds = date->tzoffset;
  }

  _sys_sync_pool_unlock();
  return ok;
}

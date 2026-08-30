#include "sync.h"
#include <pico/time.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static int64_t _sys_date_offset_seconds = 0;
static int32_t _sys_date_tzoffset_seconds = 0;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/**
 * @brief Return milliseconds since boot.
 * @return Milliseconds since boot.
 */
static int64_t _sys_date_now_ms(void) {
  absolute_time_t now = get_absolute_time();
  return (int64_t)(to_us_since_boot(now) / 1000u);
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

  int64_t ms = _sys_date_now_ms() + (_sys_date_offset_seconds * 1000);

  // Floor-divide/modulo so a negative ms (e.g. shortly after setting a
  // pre-1970 date) still splits into a well-formed (seconds, nanoseconds)
  // pair - C's / and % truncate toward zero, which would otherwise yield
  // negative nanoseconds.
  int64_t seconds = ms / 1000;
  int64_t rem_ms = ms % 1000;
  if (rem_ms < 0) {
    rem_ms += 1000;
    seconds -= 1;
  }

  date->seconds = seconds;
  date->nanoseconds = (int32_t)(rem_ms * 1000000);
  date->tzoffset = _sys_date_tzoffset_seconds;

  _sys_sync_pool_unlock();
  return true;
}

bool sys_date_set_now(const sys_date_t *date) {
  if (date == NULL || date->nanoseconds < 0 ||
      date->nanoseconds >= 1000000000) {
    return false;
  }

  _sys_sync_pool_lock();

  int64_t current_ms = _sys_date_now_ms();
  int64_t desired_ms = (date->seconds * 1000) + (date->nanoseconds / 1000000);
  _sys_date_offset_seconds = (desired_ms - current_ms) / 1000;
  _sys_date_tzoffset_seconds = date->tzoffset;

  _sys_sync_pool_unlock();
  return true;
}

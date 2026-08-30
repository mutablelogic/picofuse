#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

bool sys_date_get_now(sys_date_t *date) {
  if (date == NULL) {
    return false;
  }

  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return false;
  }

  struct tm local_tm;
  if (localtime_r(&ts.tv_sec, &local_tm) == NULL) {
    return false;
  }

  date->seconds = (int64_t)ts.tv_sec;
  date->nanoseconds = (int32_t)ts.tv_nsec;
  date->tzoffset = (int32_t)local_tm.tm_gmtoff;
  return true;
}

bool sys_date_set_now(const sys_date_t *date) {
  if (date == NULL) {
    return false;
  }

  struct timespec ts;
  ts.tv_sec = (time_t)date->seconds;
  ts.tv_nsec = (long)date->nanoseconds;

  if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
    return false;
  }

  return true;
}

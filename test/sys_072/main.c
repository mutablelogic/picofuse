#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // sys_date_set_time_utc - changes only the time-of-day, preserving the
  // calendar date already encoded in seconds.

  {
    sys_date_t date = {.seconds = 0, .nanoseconds = 0, .tzoffset = 0};
    test_assert(sys_date_set_time_utc(&date, 13, 45, 30));

    uint16_t year = 0;
    uint8_t month = 0, day = 0, hours = 0, minutes = 0, secs = 0;
    test_assert(sys_date_get_date_utc(&date, &year, &month, &day, NULL));
    test_assert(sys_date_get_time_utc(&date, &hours, &minutes, &secs));
    test_assert(year == 1970 && month == 1 && day == 1);
    test_assert(hours == 13 && minutes == 45 && secs == 30);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_set_time_utc - invalid inputs are rejected and leave the
  // date untouched; date == NULL is rejected too.

  {
    sys_date_t date = {.seconds = 123456, .nanoseconds = 0, .tzoffset = 0};
    test_assert(!sys_date_set_time_utc(&date, 24, 0, 0));  // hours
    test_assert(!sys_date_set_time_utc(&date, 0, 60, 0));  // minutes
    test_assert(!sys_date_set_time_utc(&date, 0, 0, 60));  // seconds
    test_assert(date.seconds == 123456);                   // unchanged
    test_assert(!sys_date_set_time_utc(NULL, 0, 0, 0));
  }

  ///////////////////////////////////////////////////////////////////////
  // Both setters touch only "seconds" - nanoseconds and tzoffset, which
  // neither one has any reason to know about, must survive untouched.

  {
    sys_date_t date = {
        .seconds = 0, .nanoseconds = 123456789, .tzoffset = 2 * 60 * 60};
    test_assert(sys_date_set_date_utc(&date, 2024, 2, 29));
    test_assert(date.nanoseconds == 123456789);
    test_assert(date.tzoffset == 2 * 60 * 60);

    test_assert(sys_date_set_time_utc(&date, 23, 45, 12));
    test_assert(date.nanoseconds == 123456789);
    test_assert(date.tzoffset == 2 * 60 * 60);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_set_date_utc - changes only the calendar date, preserving
  // the time-of-day already encoded in seconds.

  {
    sys_date_t date = {.seconds = 951827696, .nanoseconds = 0,
                        .tzoffset = 0}; // 2000-02-29 12:34:56 UTC
    test_assert(sys_date_set_date_utc(&date, 2024, 6, 15));

    uint16_t year = 0;
    uint8_t month = 0, day = 0, hours = 0, minutes = 0, secs = 0;
    test_assert(sys_date_get_date_utc(&date, &year, &month, &day, NULL));
    test_assert(sys_date_get_time_utc(&date, &hours, &minutes, &secs));
    test_assert(year == 2024 && month == 6 && day == 15);
    test_assert(hours == 12 && minutes == 34 && secs == 56);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_set_date_utc - invalid inputs are rejected, including the
  // leap-year-dependent day-of-month bound; date == NULL is rejected too.

  {
    sys_date_t date = {.seconds = 0, .nanoseconds = 0, .tzoffset = 0};
    test_assert(!sys_date_set_date_utc(&date, 2024, 0, 1));   // month
    test_assert(!sys_date_set_date_utc(&date, 2024, 13, 1));  // month
    test_assert(!sys_date_set_date_utc(&date, 2024, 1, 0));   // day
    test_assert(!sys_date_set_date_utc(&date, 2024, 1, 32));  // day
    test_assert(!sys_date_set_date_utc(&date, 1899, 1, 1));   // year
    test_assert(!sys_date_set_date_utc(&date, 1999, 2, 29));  // non-leap Feb
    test_assert(!sys_date_set_date_utc(&date, 1900, 2, 29));  // century, not
                                                                // div by 400
    test_assert(sys_date_set_date_utc(&date, 2000, 2, 29));   // leap Feb - ok
    test_assert(date.seconds != 0);
    test_assert(!sys_date_set_date_utc(NULL, 2024, 1, 1));
  }

  ///////////////////////////////////////////////////////////////////////
  // Building a date from scratch via the two setters and reading it back
  // must round-trip exactly.

  {
    sys_date_t date = {.seconds = 0, .nanoseconds = 0, .tzoffset = 0};
    test_assert(sys_date_set_date_utc(&date, 2038, 1, 19));
    test_assert(sys_date_set_time_utc(&date, 3, 14, 7));
    test_assert(date.seconds == 2147483647LL);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_compare_ns - same date is zero, later end is positive,
  // earlier end is negative, and a nanosecond "borrow" across a second
  // boundary combines correctly.

  {
    sys_date_t a = {.seconds = 1000, .nanoseconds = 0, .tzoffset = 0};
    sys_date_t b = {.seconds = 1000, .nanoseconds = 0, .tzoffset = 0};
    test_assert(sys_date_compare_ns(&a, &b) == 0);

    sys_date_t later = {.seconds = 1001, .nanoseconds = 0, .tzoffset = 0};
    test_assert(sys_date_compare_ns(&a, &later) == 1000000000LL);
    test_assert(sys_date_compare_ns(&later, &a) == -1000000000LL);

    sys_date_t plus_half = {
        .seconds = 1000, .nanoseconds = 500000000, .tzoffset = 0};
    test_assert(sys_date_compare_ns(&a, &plus_half) == 500000000LL);

    // 10s/800ms -> 11s/200ms: whole-second delta is 1s, but the
    // nanosecond component goes backwards within it.
    sys_date_t start = {.seconds = 10, .nanoseconds = 800000000,
                         .tzoffset = 0};
    sys_date_t end = {.seconds = 11, .nanoseconds = 200000000,
                       .tzoffset = 0};
    test_assert(sys_date_compare_ns(&start, &end) == 400000000LL);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_compare_ns - end == NULL always returns 0, regardless of
  // start; start == NULL uses the current system time.

  {
    sys_date_t any = {.seconds = 42, .nanoseconds = 0, .tzoffset = 0};
    test_assert(sys_date_compare_ns(&any, NULL) == 0);
    test_assert(sys_date_compare_ns(NULL, NULL) == 0);

    // A date far in the future compared against "now" (start == NULL)
    // must be a large positive difference - can't know the exact value,
    // but it has to be positive and in a sane ballpark (year 2030 is
    // always in the future relative to when this test can plausibly
    // run).
    sys_date_t future = {.seconds = 1893456000LL, .nanoseconds = 0,
                          .tzoffset = 0}; // 2030-01-01 00:00:00 UTC
    int64_t diff = sys_date_compare_ns(NULL, &future);
    test_assert(diff > 0);
  }

}

#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // sys_date_get_date_utc/sys_date_get_time_utc - known reference points
  // spanning the epoch, pre-1970 (negative seconds), a leap day, the
  // 1600/2400 century-boundary leap-year rule, the 1900 non-leap
  // century, and the 32-bit time_t boundary. Independently verified via
  // Python's datetime module.

  {
    struct {
      int64_t seconds;
      uint16_t year;
      uint8_t month, day, weekday, hours, minutes, secs;
    } cases[] = {
        {0, 1970, 1, 1, 4, 0, 0, 0},
        {-1, 1969, 12, 31, 3, 23, 59, 59},
        {951827696, 2000, 2, 29, 2, 12, 34, 56},
        {-11676096000LL, 1600, 1, 1, 6, 0, 0, 0},
        {13574646000LL, 2400, 2, 29, 2, 23, 0, 0},
        {-2203887477LL, 1900, 3, 1, 4, 1, 2, 3},
        {2147483647LL, 2038, 1, 19, 2, 3, 14, 7},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      sys_date_t date = {.seconds = cases[i].seconds, .nanoseconds = 0,
                          .tzoffset = 0};
      uint16_t year = 0;
      uint8_t month = 0, day = 0, weekday = 0, hours = 0, minutes = 0,
              secs = 0;
      test_assert(sys_date_get_date_utc(&date, &year, &month, &day, &weekday));
      test_assert(sys_date_get_time_utc(&date, &hours, &minutes, &secs));

      test_assert(year == cases[i].year);
      test_assert(month == cases[i].month);
      test_assert(day == cases[i].day);
      test_assert(weekday == cases[i].weekday);
      test_assert(hours == cases[i].hours);
      test_assert(minutes == cases[i].minutes);
      test_assert(secs == cases[i].secs);
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_get_date_local/sys_date_get_time_local - the tzoffset
  // actually shifts the calendar day/weekday, not just the clock, and
  // sys_date_get_date_utc/sys_date_get_time_utc on the SAME sys_date_t
  // stay anchored to seconds alone (unaffected by tzoffset).

  {
    // Epoch, one hour west of UTC - rolls back into 1969-12-31.
    sys_date_t date = {.seconds = 0, .nanoseconds = 0, .tzoffset = -3600};
    uint16_t year = 0;
    uint8_t month = 0, day = 0, weekday = 0, hours = 0, minutes = 0, secs = 0;

    test_assert(sys_date_get_date_local(&date, &year, &month, &day, &weekday));
    test_assert(sys_date_get_time_local(&date, &hours, &minutes, &secs));
    test_assert(year == 1969 && month == 12 && day == 31 && weekday == 3);
    test_assert(hours == 23 && minutes == 0 && secs == 0);

    // The same sys_date_t's UTC view is untouched.
    test_assert(sys_date_get_date_utc(&date, &year, &month, &day, &weekday));
    test_assert(sys_date_get_time_utc(&date, &hours, &minutes, &secs));
    test_assert(year == 1970 && month == 1 && day == 1 && weekday == 4);
    test_assert(hours == 0 && minutes == 0 && secs == 0);
  }

  {
    // Leap day base, ~13.9 hours east of UTC - rolls forward into March.
    sys_date_t date = {
        .seconds = 951827696, .nanoseconds = 0, .tzoffset = 50000};
    uint16_t year = 0;
    uint8_t month = 0, day = 0, weekday = 0, hours = 0, minutes = 0, secs = 0;

    test_assert(sys_date_get_date_local(&date, &year, &month, &day, &weekday));
    test_assert(sys_date_get_time_local(&date, &hours, &minutes, &secs));
    test_assert(year == 2000 && month == 3 && day == 1 && weekday == 3);
    test_assert(hours == 2 && minutes == 28 && secs == 16);
  }

  ///////////////////////////////////////////////////////////////////////
  // Individual output parameters are optional (NULL-safe) and independent
  // - requesting a subset must not touch or require the others.

  {
    sys_date_t date = {.seconds = 951827696, .nanoseconds = 0, .tzoffset = 0};
    uint8_t month = 0;
    test_assert(sys_date_get_date_utc(&date, NULL, &month, NULL, NULL));
    test_assert(month == 2);

    uint8_t minutes = 0;
    test_assert(sys_date_get_time_utc(&date, NULL, &minutes, NULL));
    test_assert(minutes == 34);

    test_assert(sys_date_get_date_utc(&date, NULL, NULL, NULL, NULL));
    test_assert(sys_date_get_time_utc(&date, NULL, NULL, NULL));
  }

  ///////////////////////////////////////////////////////////////////////
  // date == NULL means "use the current system time" - can't know the
  // exact value, but the result must be internally consistent with a
  // sys_date_get_now() fetched immediately after, within a generous
  // tolerance for the time elapsed between the two calls.

  {
    uint16_t year = 0;
    uint8_t month = 0, day = 0, weekday = 0, hours = 0, minutes = 0,
            secs = 0;
    test_assert(sys_date_get_date_utc(NULL, &year, &month, &day, &weekday));
    test_assert(sys_date_get_time_utc(NULL, &hours, &minutes, &secs));
    test_assert(hours < 24 && minutes < 60 && secs < 60);
    test_assert(month >= 1 && month <= 12);
    test_assert(day >= 1 && day <= 31);
    test_assert(weekday <= 6);

    sys_date_t now;
    test_assert(sys_date_get_now(&now));
    uint16_t now_year = 0;
    test_assert(sys_date_get_date_utc(&now, &now_year, NULL, NULL, NULL));
    test_assert(now_year == year);
  }

}

#include <picofuse/sys.h>
#include <test/test.h>

// 0000-01-01T00:00:00Z and 65535-12-31T23:59:59Z
#define FIRST INT64_C(-62167219200)
#define LAST INT64_C(2005949145599)

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // sys_date_add_ns - carries into seconds, both ways

  {
    sys_date_t date = {.seconds = 10, .nanoseconds = 200000000, .tzoffset = 60};
    test_assert(sys_date_add_ns(&date, 1500000000LL));
    test_assert(date.seconds == 11 && date.nanoseconds == 700000000);
    test_assert(sys_date_add_ns(&date, 300000000LL));
    test_assert(date.seconds == 12 && date.nanoseconds == 0);
    test_assert(sys_date_add_ns(&date, -1LL));
    test_assert(date.seconds == 11 && date.nanoseconds == 999999999);
    test_assert(sys_date_add_ns(&date, -2999999999LL));
    test_assert(date.seconds == 9 && date.nanoseconds == 0);
    test_assert(sys_date_add_ns(&date, 0));
    test_assert(date.seconds == 9 && date.nanoseconds == 0);
    test_assert(date.tzoffset == 60);

    // Before 1970
    date = (sys_date_t){.seconds = 0, .nanoseconds = 0};
    test_assert(sys_date_add_ns(&date, -1LL));
    test_assert(date.seconds == -1 && date.nanoseconds == 999999999);

    // The largest durations
    date = (sys_date_t){.seconds = 0, .nanoseconds = 0};
    test_assert(sys_date_add_ns(&date, INT64_MAX));
    test_assert(date.seconds == INT64_MAX / 1000000000LL);
    test_assert(date.nanoseconds == (int32_t)(INT64_MAX % 1000000000LL));
    date = (sys_date_t){.seconds = 0, .nanoseconds = 0};
    test_assert(sys_date_add_ns(&date, INT64_MIN));
    test_assert(date.seconds == INT64_MIN / 1000000000LL - 1);
    test_assert(date.nanoseconds ==
                (int32_t)(1000000000LL + INT64_MIN % 1000000000LL));

    // A carry which cancels out the seconds of the duration
    date = (sys_date_t){.seconds = INT64_MIN, .nanoseconds = 1500000000};
    test_assert(sys_date_add_ns(&date, -1000000000LL));
    test_assert(date.seconds == INT64_MIN && date.nanoseconds == 500000000);
    date = (sys_date_t){.seconds = INT64_MAX, .nanoseconds = -500000000};
    test_assert(sys_date_add_ns(&date, 1000000000LL));
    test_assert(date.seconds == INT64_MAX && date.nanoseconds == 500000000);

    // Nanoseconds out of range are put right
    date = (sys_date_t){.seconds = 0, .nanoseconds = 1500000000};
    test_assert(sys_date_add_ns(&date, 0));
    test_assert(date.seconds == 1 && date.nanoseconds == 500000000);
    date = (sys_date_t){.seconds = 0, .nanoseconds = -1};
    test_assert(sys_date_add_ns(&date, 0));
    test_assert(date.seconds == -1 && date.nanoseconds == 999999999);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_add_ns - overflow leaves the date unchanged

  {
    sys_date_t date = {.seconds = INT64_MAX, .nanoseconds = 999999999};
    test_assert(!sys_date_add_ns(&date, 1));
    test_assert(date.seconds == INT64_MAX && date.nanoseconds == 999999999);
    test_assert(sys_date_add_ns(&date, -1));
    test_assert(date.seconds == INT64_MAX && date.nanoseconds == 999999998);
    date = (sys_date_t){.seconds = INT64_MIN, .nanoseconds = 0};
    test_assert(!sys_date_add_ns(&date, -1));
    test_assert(date.seconds == INT64_MIN && date.nanoseconds == 0);
    test_assert(!sys_date_add_ns(NULL, 0));
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_date_compare_ns - saturates when too far apart

  {
    sys_date_t a = {.seconds = 0};
    sys_date_t near = {.seconds = INT64_MAX / 1000000000LL};
    sys_date_t far = {.seconds = INT64_MAX / 1000000000LL + 1};
    test_assert(sys_date_compare_ns(&a, &near) ==
                INT64_MAX / 1000000000LL * 1000000000LL);
    test_assert(sys_date_compare_ns(&a, &far) == INT64_MAX);
    test_assert(sys_date_compare_ns(&far, &a) == INT64_MIN);
    // The largest differences which fit, where the nanoseconds take it back
    // into range
    sys_date_t b = {.seconds = 0, .nanoseconds = 999999999};
    sys_date_t c = {.seconds = 9223372037, .nanoseconds = 0};
    test_assert(sys_date_compare_ns(&b, &c) == 9223372036000000001LL);
    test_assert(sys_date_compare_ns(&c, &b) == -9223372036000000001LL);
    sys_date_t d = {.seconds = 9223372036, .nanoseconds = 854775807};
    test_assert(sys_date_compare_ns(&a, &d) == INT64_MAX);
    d.nanoseconds = 854775808;
    test_assert(sys_date_compare_ns(&a, &d) == INT64_MAX);
    sys_date_t e = {.seconds = -9223372036, .nanoseconds = 0};
    sys_date_t f = {.seconds = 0, .nanoseconds = 854775808};
    test_assert(sys_date_compare_ns(&f, &e) == INT64_MIN);
    sys_date_t min = {.seconds = INT64_MIN};
    sys_date_t max = {.seconds = INT64_MAX, .nanoseconds = 999999999};
    test_assert(sys_date_compare_ns(&min, &max) == INT64_MAX);
    test_assert(sys_date_compare_ns(&max, &min) == INT64_MIN);
  }

  ///////////////////////////////////////////////////////////////////////
  // Years have to be 0-65535

  {
    uint16_t year = 1;
    uint8_t month = 0, day = 0, hours = 99;
    char buf[48];

    sys_date_t date = {.seconds = FIRST};
    test_assert(sys_date_get_date_utc(&date, &year, &month, &day, NULL));
    test_assert(year == 0 && month == 1 && day == 1);
    date.seconds = LAST;
    test_assert(sys_date_get_date_utc(&date, &year, &month, &day, NULL));
    test_assert(year == 65535 && month == 12 && day == 31);
    test_assert(sys_date_to_string(&date, sys_date_format_iso8601, buf,
                                   sizeof(buf)) == 21);
    test_assert_strequal(buf, "65535-12-31T23:59:59Z");

    // One second either side
    year = 1;
    date.seconds = LAST + 1;
    test_assert(!sys_date_get_date_utc(&date, &year, NULL, NULL, NULL));
    test_assert(!sys_date_get_time_utc(&date, &hours, NULL, NULL));
    test_assert(year == 1 && hours == 99);
    test_assert(sys_date_to_string(&date, sys_date_format_iso8601, buf,
                                   sizeof(buf)) == 0 &&
                buf[0] == '\0');
    test_assert(sys_date_to_string(&date, sys_date_format_rfc2822, buf,
                                   sizeof(buf)) == 0);
    date.seconds = FIRST - 1;
    test_assert(!sys_date_get_date_utc(&date, &year, NULL, NULL, NULL));

    // Local, with an offset which takes it out of range
    date.seconds = LAST;
    date.tzoffset = 3600;
    test_assert(!sys_date_get_date_local(&date, &year, NULL, NULL, NULL));
    test_assert(!sys_date_get_time_local(&date, &hours, NULL, NULL));
    test_assert(sys_date_to_string(&date, sys_date_format_log, buf,
                                   sizeof(buf)) == 0);
    test_assert(sys_date_get_date_utc(&date, &year, NULL, NULL, NULL));

    // The largest dates, where an offset would overflow
    date.seconds = INT64_MAX;
    test_assert(!sys_date_get_date_utc(&date, &year, NULL, NULL, NULL));
    test_assert(!sys_date_get_date_local(&date, &year, NULL, NULL, NULL));
    date.seconds = INT64_MIN;
    date.tzoffset = -3600;
    test_assert(!sys_date_get_time_local(&date, &hours, NULL, NULL));
    test_assert(sys_date_to_string(&date, sys_date_format_iso8601, buf,
                                   sizeof(buf)) == 0);

    // The largest offsets, which are written without overflowing
    date = (sys_date_t){.seconds = 0, .tzoffset = INT32_MIN};
    test_assert(sys_date_to_string(&date, sys_date_format_iso8601, buf,
                                   sizeof(buf)) == 29);
    test_assert_strequal(buf, "1901-12-13T20:45:52-596523:14");
    date.tzoffset = INT32_MAX;
    test_assert(sys_date_to_string(&date, sys_date_format_iso8601, buf,
                                   sizeof(buf)) == 29);
    test_assert_strequal(buf, "2038-01-19T03:14:07+596523:14");

    // Setting the date or time of one out of range
    date = (sys_date_t){.seconds = LAST + 1};
    test_assert(!sys_date_set_time_utc(&date, 0, 0, 0));
    test_assert(date.seconds == LAST + 1);
  }
}

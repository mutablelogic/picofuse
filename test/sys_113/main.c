#include <picofuse/sys.h>
#include <test/test.h>

// 2026-09-06T11:37:05Z, a Sunday
#define SAMPLE INT64_C(1788694625)

static sys_date_t parse(const char *str, sys_date_format_t format) {
  sys_date_t date = {.seconds = 1, .nanoseconds = 2, .tzoffset = 3};
  test_assert(sys_date_parse(str, 0, format, &date));
  return date;
}

static void invalid(const char *str, sys_date_format_t format) {
  sys_date_t date = {.seconds = 1, .nanoseconds = 2, .tzoffset = 3};
  test_assert(!sys_date_parse(str, 0, format, &date));
  test_assert(date.seconds == 1 && date.nanoseconds == 2 && date.tzoffset == 3);
}

// Writes a date and reads it back
static void round_trip(sys_date_t date, sys_date_format_t format) {
  char buf[48];
  test_assert(sys_date_to_string(&date, format, buf, sizeof(buf)) > 0);
  sys_date_t back = parse(buf, format);
  test_assert(back.seconds == date.seconds);
}

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // ISO 8601

  {
    sys_date_t date = parse("2026-09-06T11:37:05Z", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE && date.nanoseconds == 0);
    test_assert(date.tzoffset == 0);
    date = parse("2026-09-06t11:37:05z", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE);

    // Offsets, which keep the moment the same
    date = parse("2026-09-06T12:37:05+01:00", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE && date.tzoffset == 3600);
    date = parse("2026-09-06T06:07:05-05:30", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE && date.tzoffset == -19800);
    date = parse("2026-09-06T11:37:05+00:00", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE && date.tzoffset == 0);
    date = parse("2026-09-07T00:37:05+13:00", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE);

    // Fractions of a second
    date = parse("2026-09-06T11:37:05.25Z", sys_date_format_iso8601);
    test_assert(date.seconds == SAMPLE && date.nanoseconds == 250000000);
    date = parse("2026-09-06T11:37:05.123456789Z", sys_date_format_iso8601);
    test_assert(date.nanoseconds == 123456789);
    date = parse("2026-09-06T11:37:05.1234567899Z", sys_date_format_iso8601);
    test_assert(date.nanoseconds == 123456789);
    date = parse("2026-09-06T11:37:05.000000001Z", sys_date_format_iso8601);
    test_assert(date.nanoseconds == 1);

    // Just a date
    date = parse("2026-09-06", sys_date_format_iso8601);
    test_assert(date.seconds == INT64_C(1788652800) && date.tzoffset == 0);

    // The first and last years, and before 1970
    date = parse("0000-01-01T00:00:00Z", sys_date_format_iso8601);
    test_assert(date.seconds == INT64_C(-62167219200));
    date = parse("65535-12-31T23:59:59Z", sys_date_format_iso8601);
    test_assert(date.seconds == INT64_C(2005949145599));
    date = parse("1900-01-01T00:00:00Z", sys_date_format_iso8601);
    test_assert(date.seconds == INT64_C(-2208988800));
    date = parse("2024-02-29T23:59:59Z", sys_date_format_iso8601);
    test_assert(date.seconds == INT64_C(1709251199));

    const char *bad[] = {"",
                         "2026-09-06T11:37:05",
                         "2026-09-06 11:37:05Z",
                         "2026-09-06T11:37Z",
                         "2026-9-06T11:37:05Z",
                         "26-09-06T11:37:05Z",
                         "65536-01-01T00:00:00Z",
                         "2026-09-06T11:37:05.Z",
                         "2026-09-06T11:37:05+01",
                         "2026-09-06T11:37:05+0100",
                         "2026-09-06T11:37:05+24:00",
                         "2026-09-06T11:37:05+01:60",
                         "2026-09-06T11:37:05Z ",
                         " 2026-09-06T11:37:05Z",
                         "2026-02-30T00:00:00Z",
                         "2025-02-29",
                         "2026-13-01",
                         "2026-00-01",
                         "2026-01-00",
                         "2026-09-06T24:00:00Z",
                         "2026-09-06T23:60:00Z",
                         "2026-09-06T23:59:60Z",
                         "2026-09-06T",
                         "2026-09-06X",
                         "Sun, 06 Sep 2026 11:37:05 GMT"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      invalid(bad[i], sys_date_format_iso8601);
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // RFC 2822

  {
    sys_date_t date =
        parse("Sun, 06 Sep 2026 11:37:05 GMT", sys_date_format_rfc2822);
    test_assert(date.seconds == SAMPLE && date.tzoffset == 0);
    test_assert(parse("06 Sep 2026 11:37:05 GMT", sys_date_format_rfc2822)
                    .seconds == SAMPLE);
    test_assert(parse("Sun, 6 Sep 2026 11:37:05 UTC", sys_date_format_rfc2822)
                    .seconds == SAMPLE);
    test_assert(parse("Sun, 06 Sep 2026 11:37:05 UT", sys_date_format_rfc2822)
                    .seconds == SAMPLE);
    test_assert(parse("Sun, 06 Sep 2026 11:37:05 Z", sys_date_format_rfc2822)
                    .seconds == SAMPLE);
    test_assert(parse("Sun,  06  Sep  2026  11:37:05  GMT",
                      sys_date_format_rfc2822)
                    .seconds == SAMPLE);
    test_assert(parse("Sun, 06 Sep 2026 11:37 GMT", sys_date_format_rfc2822)
                    .seconds == SAMPLE - 5);

    // Offsets, where the weekday is the one in the date's own zone
    date = parse("Sun, 06 Sep 2026 12:37:05 +0100", sys_date_format_rfc2822);
    test_assert(date.seconds == SAMPLE && date.tzoffset == 3600);
    date = parse("Mon, 07 Sep 2026 00:37:05 +1300", sys_date_format_rfc2822);
    test_assert(date.seconds == SAMPLE && date.tzoffset == 46800);
    date = parse("Sun, 06 Sep 2026 06:07:05 -0530", sys_date_format_rfc2822);
    test_assert(date.seconds == SAMPLE && date.tzoffset == -19800);
    test_assert(parse("Tue, 1 Sep 2026 08:05:00 GMT", sys_date_format_rfc2822)
                    .seconds == INT64_C(1788249900));

    const char *bad[] = {"",
                         "Mon, 06 Sep 2026 11:37:05 GMT",
                         "Sun, 07 Sep 2026 00:37:05 +1300",
                         "Sunday, 06 Sep 2026 11:37:05 GMT",
                         "Sun 06 Sep 2026 11:37:05 GMT",
                         "Sun,06 Sep 2026 11:37:05 GMT",
                         "Sun, 06 sep 2026 11:37:05 GMT",
                         "Sun, 06 September 2026 11:37:05 GMT",
                         "Sun, 06 Sep 26 11:37:05 GMT",
                         "Sun, 06 Sep 2026 11:37:05",
                         "Sun, 06 Sep 2026 11:37:05 EST",
                         "Sun, 06 Sep 2026 11:37:05 +01:00",
                         "Sun, 06 Sep 2026 11:37:05 +2400",
                         "Sun, 06 Sep 2026 11:37:05 GMT ",
                         "Sun, 31 Sep 2026 11:37:05 GMT",
                         "Sun, 006 Sep 2026 11:37:05 GMT",
                         "2026-09-06T11:37:05Z"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      invalid(bad[i], sys_date_format_rfc2822);
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // Log, which is UTC

  {
    sys_date_t date = parse("2026-09-06 11:37:05", sys_date_format_log);
    test_assert(date.seconds == SAMPLE && date.tzoffset == 0);
    const char *bad[] = {"", "2026-09-06T11:37:05", "2026-09-06 11:37:05Z",
                         "2026-09-06 11:37", "2026-09-06  11:37:05",
                         "2026-09-06"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      invalid(bad[i], sys_date_format_log);
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // What's written can be read back, in every format and zone

  {
    int32_t offsets[] = {0, 3600, -19800, 46800};
    int64_t seconds[] = {SAMPLE, 0, -1, INT64_C(-2208988800),
                         INT64_C(1709251199)};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
      for (size_t j = 0; j < sizeof(seconds) / sizeof(seconds[0]); j++) {
        sys_date_t date = {.seconds = seconds[j], .tzoffset = offsets[i]};
        round_trip(date, sys_date_format_iso8601);
        round_trip(date, sys_date_format_rfc2822);
        round_trip(date, sys_date_format_log);
      }
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // With a length, and those which can't be parsed at all

  {
    sys_date_t date = {0};
    test_assert(sys_date_parse("2026-09-06T11:37:05Z", 10,
                               sys_date_format_iso8601, &date));
    test_assert(date.seconds == INT64_C(1788652800));
    test_assert(!sys_date_parse(NULL, 0, sys_date_format_iso8601, &date));
    test_assert(!sys_date_parse("2026-09-06", 0, sys_date_format_iso8601, NULL));
    test_assert(!sys_date_parse("2026-09-06", 0, (sys_date_format_t)99, &date));
  }
}

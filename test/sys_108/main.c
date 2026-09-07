#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {
  char buf[64];

  // 2026-09-06 11:59:25 UTC.
  sys_date_t date = {.seconds = 1788695965, .nanoseconds = 0, .tzoffset = 0};

  test_assert(sys_date_to_string(&date, sys_date_format_iso8601, buf,
                                 sizeof(buf)) == 20);
  test_assert_strequal(buf, "2026-09-06T11:59:25Z");

  test_assert(sys_date_to_string(&date, sys_date_format_rfc2822, buf,
                                 sizeof(buf)) == 29);
  test_assert_strequal(buf, "Sun, 06 Sep 2026 11:59:25 GMT");

  test_assert(sys_date_to_string(&date, sys_date_format_log, buf,
                                 sizeof(buf)) == 19);
  test_assert_strequal(buf, "2026-09-06 11:59:25");

  // Non-zero tzoffset shifts iso8601/log, but never rfc2822 (always GMT).
  sys_date_t date_tz = {
      .seconds = 1788695965, .nanoseconds = 0, .tzoffset = 3600};
  sys_date_to_string(&date_tz, sys_date_format_iso8601, buf, sizeof(buf));
  test_assert_strequal(buf, "2026-09-06T12:59:25+01:00");
  sys_date_to_string(&date_tz, sys_date_format_log, buf, sizeof(buf));
  test_assert_strequal(buf, "2026-09-06 12:59:25");
  sys_date_to_string(&date_tz, sys_date_format_rfc2822, buf, sizeof(buf));
  test_assert_strequal(buf, "Sun, 06 Sep 2026 11:59:25 GMT");

  // Negative tzoffset.
  sys_date_t date_neg = {
      .seconds = 1788695965, .nanoseconds = 0, .tzoffset = -18000};
  sys_date_to_string(&date_neg, sys_date_format_iso8601, buf, sizeof(buf));
  test_assert_strequal(buf, "2026-09-06T06:59:25-05:00");

  // NULL date uses the current system time - just check it succeeds and
  // is well-formed (fixed-width iso8601 is always exactly 20 characters
  // for a zero tzoffset).
  sys_date_t now = {0};
  test_assert(sys_date_get_now(&now));
  size_t n = sys_date_to_string(NULL, sys_date_format_log, buf, sizeof(buf));
  test_assert(n == 19);

  // Truncation semantics match sys_sprintf(): the return value is the
  // length that would have been written, not what actually fit.
  char tiny[8];
  size_t want =
      sys_date_to_string(&date, sys_date_format_iso8601, tiny, sizeof(tiny));
  test_assert(want == 20);
  test_assert(strlen(tiny) == sizeof(tiny) - 1);
}

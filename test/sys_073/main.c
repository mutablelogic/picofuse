#include <picofuse/sys.h>
#include <test/test.h>

#ifdef SYSTEM_NAME_PICO
// Whether two dates are within ms milliseconds of each other
static bool within_ms(const sys_date_t *a, const sys_date_t *b, int64_t ms) {
  int64_t diff = sys_date_compare_ns(a, b);
  return (diff < 0 ? -diff : diff) < ms * 1000000;
}
#endif

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // NULL rejection - safe and side-effect-free on every platform.

  test_assert(!sys_date_get_now(NULL));
  test_assert(!sys_date_set_now(NULL));

  ///////////////////////////////////////////////////////////////////////
  // sys_date_get_now's own output is well-formed.

  {
    sys_date_t now = {0};
    test_assert(sys_date_get_now(&now));
    test_assert(now.nanoseconds >= 0 && now.nanoseconds < 1000000000);
  }

#ifdef SYSTEM_NAME_PICO
  ///////////////////////////////////////////////////////////////////////
  // sys_date_set_now's nanoseconds-range validation, and a full set/get
  // round trip - only exercised on Pico, where the clock is a synthetic
  // boot-uptime-plus-offset, never a real hardware/OS clock. On POSIX
  // this would call clock_settime() on the actual system clock, which is
  // far too invasive for an automated test regardless of whether it
  // happens to succeed under current privileges.

  {
    sys_date_t before = {0};
    test_assert(sys_date_get_now(&before));

    sys_date_t target = before;
    target.seconds += 2;
    target.nanoseconds = 250000000;
    target.tzoffset = 90 * 60;

    sys_date_t invalid = target;
    invalid.nanoseconds = 1000000000;
    test_assert(!sys_date_set_now(&invalid));
    invalid.nanoseconds = -1;
    test_assert(!sys_date_set_now(&invalid));

    test_assert(sys_date_set_now(&target));

    sys_date_t after = {0};
    test_assert(sys_date_get_now(&after));
    test_assert(after.tzoffset == target.tzoffset);

    // To within a few milliseconds, not a second (#37)
    test_assert(within_ms(&target, &after, 5));
  }

  ///////////////////////////////////////////////////////////////////////
  // The issue's example: a whole second, set when the boot clock is part
  // way through one, reads back as that second - not up to 999 ms early

  {
    sys_date_t target = {.seconds = 1788694625, .nanoseconds = 0};
    test_assert(sys_date_set_now(&target));
    sys_date_t after = {0};
    test_assert(sys_date_get_now(&after));
    test_assert(within_ms(&target, &after, 5));
  }

  ///////////////////////////////////////////////////////////////////////
  // Before 1970, and before the boot clock started, it's out in neither
  // direction, and nanoseconds stay well-formed

  {
    sys_date_t target = {.seconds = -100, .nanoseconds = 750000000};
    test_assert(sys_date_set_now(&target));
    sys_date_t after = {0};
    test_assert(sys_date_get_now(&after));
    test_assert(after.nanoseconds >= 0 && after.nanoseconds < 1000000000);
    test_assert(within_ms(&target, &after, 5));

    // And it keeps time
    sys_sleep_ms(100);
    test_assert(sys_date_get_now(&after));
    int64_t ms = sys_date_compare_ns(&target, &after) / 1000000;
    test_assert(ms >= 100 && ms < 110);
  }

  ///////////////////////////////////////////////////////////////////////
  // Dates which don't fit in microseconds are refused, and the clock is
  // left as it was

  {
    sys_date_t target = {.seconds = 1788694625, .nanoseconds = 0};
    test_assert(sys_date_set_now(&target));

    sys_date_t huge = {.seconds = INT64_MAX, .nanoseconds = 0};
    test_assert(!sys_date_set_now(&huge));
    huge.seconds = INT64_MIN;
    test_assert(!sys_date_set_now(&huge));
    huge.seconds = INT64_MAX / 1000000; // fits, but not with the nanoseconds
    huge.nanoseconds = 999999999;
    test_assert(!sys_date_set_now(&huge));

    sys_date_t after = {0};
    test_assert(sys_date_get_now(&after));
    test_assert(within_ms(&target, &after, 5));
  }
#endif

}

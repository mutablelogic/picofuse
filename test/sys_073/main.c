#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

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
    int64_t diff = sys_date_compare_ns(&target, &after);
    test_assert((diff < 0 ? -diff : diff) < 500000000LL);
    test_assert(after.tzoffset == target.tzoffset);
  }
#endif

}

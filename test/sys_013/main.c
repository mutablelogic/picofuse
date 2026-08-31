#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

test_main_sys() {

  // sys_init() already called sys_timestamp_ms() once to establish the
  // baseline, so the very next call should be small - just startup
  // overhead, not an arbitrary wall-clock value.
  uint64_t t0 = sys_timestamp_ms();
  test_assert(t0 < 1000);

  // Two calls back-to-back, no sleep in between: never goes backwards.
  uint64_t t1 = sys_timestamp_ms();
  test_assert(t1 >= t0);

  // sys_sleep_ms(0) must not block for any meaningful duration.
  uint64_t before_zero = sys_timestamp_ms();
  sys_sleep_ms(0);
  uint64_t after_zero = sys_timestamp_ms();
  test_assert((after_zero - before_zero) < 20);

  // A real sleep must advance the timestamp by at least the requested
  // duration (sleep primitives guarantee "at least", never less), with a
  // generous upper bound so this doesn't flake under a loaded CI machine.
  uint64_t before_sleep = sys_timestamp_ms();
  sys_sleep_ms(50);
  uint64_t after_sleep = sys_timestamp_ms();
  uint64_t elapsed = after_sleep - before_sleep;
  test_assert(elapsed >= 50);
  test_assert(elapsed < 500);

  // Two sequential sleeps accumulate: at least the sum of both durations.
  uint64_t before_two = sys_timestamp_ms();
  sys_sleep_ms(30);
  sys_sleep_ms(30);
  uint64_t after_two = sys_timestamp_ms();
  test_assert((after_two - before_two) >= 60);

}

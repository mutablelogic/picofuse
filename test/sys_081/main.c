#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

static sys_atomic_t _tick_count;

static void periodic_callback(sys_timer_t *timer) {
  // userdata must be retrievable from within the callback too.
  const char *userdata = (const char *)sys_timer_userdata(timer);
  test_assert(userdata != NULL);
  test_assert(strcmp(userdata, "periodic") == 0);
  sys_atomic_inc(&_tick_count);
}

test_main_sys() {
  sys_atomic_init(&_tick_count, 0);

  sys_timer_t *t = sys_timer_init(20, periodic_callback, "periodic");
  test_assert(t != NULL);

  // userdata is available before the timer is even started.
  test_assert(strcmp((const char *)sys_timer_userdata(t), "periodic") == 0);

  test_assert(sys_timer_start(t));

  // Wait for several ticks, with a generous timeout.
  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&_tick_count) < 5) {
    test_assert(sys_timestamp_ms() - start < 5000);
    sys_sleep_ms(5);
  }

  sys_timer_deinit(t);

  // No further ticks occur once deinitialized.
  uint32_t count_at_deinit = sys_atomic_get(&_tick_count);
  sys_sleep_ms(100);
  test_assert(sys_atomic_get(&_tick_count) == count_at_deinit);

  // userdata on a deinitialized (now invalid) handle returns NULL.
  test_assert(sys_timer_userdata(t) == NULL);

}

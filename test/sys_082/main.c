#include <picofuse/sys.h>
#include <test/test.h>

static sys_atomic_t _fire_count;

static void oneshot_callback(sys_timer_t *timer) {
  sys_atomic_inc(&_fire_count);
  // Safe to call from within the timer callback itself (documented
  // behavior) - this is how one-shot semantics are implemented.
  sys_timer_deinit(timer);
}

test_main_sys(0) {
  sys_atomic_init(&_fire_count, 0);

  sys_timer_t *t = sys_timer_init(20, oneshot_callback, NULL);
  test_assert(t != NULL);
  test_assert(sys_timer_start(t));

  // Wait for the single fire.
  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&_fire_count) == 0) {
    test_assert(sys_timestamp_ms() - start < 5000);
    sys_sleep_ms(5);
  }

  // Give it plenty of opportunity to have fired again if the self-deinit
  // hadn't actually stopped it.
  sys_sleep_ms(200);
  test_assert(sys_atomic_get(&_fire_count) == 1);

  // The pool slot is free again - a fresh timer can reuse it.
  sys_timer_t *again = sys_timer_init(1000, oneshot_callback, NULL);
  test_assert(again != NULL);
  sys_timer_deinit(again);

}

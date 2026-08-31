#include <picofuse/sys.h>
#include <test/test.h>

static void noop_callback(sys_timer_t *timer) { (void)timer; }

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////////
  // Argument validation.

  test_assert(sys_timer_init(0, noop_callback, NULL) == NULL);
  test_assert(sys_timer_init(10, NULL, NULL) == NULL);

  ///////////////////////////////////////////////////////////////////////////
  // A fresh timer is idle until started; starting it twice fails the
  // second time; deinit releases it back to the pool.

  sys_timer_t *t = sys_timer_init(1000, noop_callback, NULL);
  test_assert(t != NULL);
  test_assert(sys_timer_start(t));
  test_assert(!sys_timer_start(t));
  sys_timer_deinit(t);

  ///////////////////////////////////////////////////////////////////////////
  // Pool exhaustion: SYS_TIMER_CAPACITY timers can be allocated, the next
  // one fails until a slot is released.

  sys_timer_t *timers[SYS_TIMER_CAPACITY];
  for (size_t i = 0; i < SYS_TIMER_CAPACITY; i++) {
    timers[i] = sys_timer_init(1000, noop_callback, NULL);
    test_assert(timers[i] != NULL);
  }
  test_assert(sys_timer_init(1000, noop_callback, NULL) == NULL);

  sys_timer_deinit(timers[0]);
  timers[0] = sys_timer_init(1000, noop_callback, NULL);
  test_assert(timers[0] != NULL);

  for (size_t i = 0; i < SYS_TIMER_CAPACITY; i++) {
    sys_timer_deinit(timers[i]);
  }

}

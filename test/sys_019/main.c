#include <picofuse/sys.h>
#include <test/test.h>

// Pico uses one worker
#if defined(SYSTEM_NAME_PICO)
#define NUM_WAITERS 1
#else
#define NUM_WAITERS SYS_THREAD_CAPACITY
#endif

static sys_waitgroup_t *_wg;
static sys_atomic_t _waiter_phase;
static sys_atomic_t _released_count;

static void waiter(void *arg) {
  (void)arg;
  sys_atomic_set(&_waiter_phase, 1);
  // Increment the released count once the waiter is unblocked.
  sys_waitgroup_wait(_wg);
  sys_atomic_set(&_waiter_phase, 2);
  sys_atomic_inc(&_released_count);
}

static void dispatch(sys_thread_func_t func) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, NULL, 1));
#else
  test_assert(sys_thread_create(func, NULL));
#endif
}

test_main_sys(0) {
  sys_atomic_init(&_waiter_phase, 0);
  sys_atomic_init(&_released_count, 0);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));

  for (int i = 0; i < NUM_WAITERS; i++) {
    dispatch(waiter);
  }

  // Wait for all waiters to start blocking on the wait group before marking it
  // done.
  sys_sleep_ms(100);
  test_assert(sys_atomic_get(&_waiter_phase) == 1);

  // Mark the wait group as done, releasing all waiters.
  test_assert(sys_waitgroup_done(_wg));

  // All waiters must eventually observe the release.
  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&_released_count) < NUM_WAITERS) {
    test_assert(sys_timestamp_ms() - start < 5000);
    sys_sleep_ms(10);
  }
  test_assert(sys_atomic_get(&_waiter_phase) == 2);

  sys_waitgroup_deinit(_wg);
}

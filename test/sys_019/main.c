#include <picofuse/sys.h>
#include <test/test.h>

// Pico only has one spare core (core1), so true simultaneous multi-waiter
// concurrency is only exercised on host platforms, which can spawn up to
// the full thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WAITERS 1
#else
#define NUM_WAITERS SYS_THREAD_CAPACITY
#endif

static sys_waitgroup_t *_wg;
static sys_atomic_t _released_count;

static void waiter(void *arg) {
  (void)arg;
  // Every waiter must be released together once the counter hits zero -
  // not just the first one, and not only ones that happen to arrive late.
  sys_waitgroup_wait(_wg);
  sys_atomic_inc(&_released_count);
}

static void dispatch(sys_thread_func_t func) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, NULL, 1));
#else
  test_assert(sys_thread_create(func, NULL));
#endif
}

int main(void) {
  sys_init();
  sys_atomic_init(&_released_count, 0);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));

  for (int i = 0; i < NUM_WAITERS; i++) {
    dispatch(waiter);
  }

  // Give every waiter a generous head start to actually block inside
  // sys_waitgroup_wait() before the counter drops to zero, so this
  // genuinely exercises several threads blocked on the same wait group at
  // once - not just threads that happen to arrive after it's already done.
  sys_sleep_ms(100);

  test_assert(sys_waitgroup_done(_wg));

  // All waiters must eventually observe the release. Poll with a generous
  // timeout rather than a fixed sleep, so this fails deterministically on a
  // real bug (a waiter that's never released) instead of hanging forever.
  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&_released_count) < NUM_WAITERS) {
    test_assert(sys_timestamp_ms() - start < 5000);
    sys_sleep_ms(10);
  }

  sys_waitgroup_deinit(_wg);

  sys_exit();
  return 0;
}

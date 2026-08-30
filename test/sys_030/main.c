#include <picofuse/sys.h>
#include <test/test.h>

static sys_waitgroup_t *_wg;
static sys_atomic_t _released;

static void dispatch(sys_thread_func_t func) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, NULL, 1));
#else
  test_assert(sys_thread_create(func, NULL));
#endif
}

// No sys_waitgroup_done() will ever be called for this add(1) - the
// counter can never reach zero on its own. This deliberately violates the
// documented precondition ("no threads should be waiting... when
// sys_waitgroup_deinit() is called") to verify deinit's safety net
// actually releases a genuinely-stranded waiter instead of leaving it
// blocked forever.
static void stuck_waiter(void *arg) {
  (void)arg;
  sys_waitgroup_wait(_wg);
  sys_atomic_set(&_released, 1);
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));

  sys_atomic_init(&_released, 0);
  dispatch(stuck_waiter);

  // Give the waiter a real chance to actually be blocked before deinit.
  sys_sleep_ms(100);

  uint64_t before = sys_timestamp_ms();
  sys_waitgroup_deinit(_wg);

  // Poll (bounded) for the waiter to actually finish. deinit's internal
  // safety net must release it promptly - a real bug here would mean it's
  // stuck forever, so this is bounded to fail cleanly instead of hanging.
  while (sys_atomic_get(&_released) == 0) {
    test_assert(sys_timestamp_ms() - before < 2000);
    sys_sleep_ms(5);
  }
  uint64_t elapsed = sys_timestamp_ms() - before;
  test_assert(elapsed < 500);

  sys_exit();
  return 0;
}

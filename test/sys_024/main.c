#include <picofuse/sys.h>
#include <test/test.h>

// Host platforms back sys_thread_create with a SYS_THREAD_CAPACITY-sized
// static pool; Pico has a hardware capacity of exactly one (core1), tracked
// via a single busy flag rather than a pool array. Either way, "exhausted"
// means the same thing: one more dispatch attempt must fail gracefully.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

static sys_atomic_t _release;
static sys_waitgroup_t *_wg;

static void blocking_worker(void *arg) {
  (void)arg;
  while (sys_atomic_get(&_release) == 0) {
    sys_sleep_ms(5);
  }
  test_assert(sys_waitgroup_done(_wg));
}

static bool try_dispatch(void) {
#if defined(SYSTEM_NAME_PICO)
  return sys_thread_create_on_core(blocking_worker, NULL, 1);
#else
  return sys_thread_create(blocking_worker, NULL);
#endif
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);
  sys_atomic_init(&_release, 0);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  // Fill every slot with a worker that blocks until released.
  for (int i = 0; i < NUM_WORKERS; i++) {
    test_assert(try_dispatch());
  }

  // With every slot occupied, one more dispatch must fail gracefully
  // rather than silently succeeding beyond capacity.
  test_assert(!try_dispatch());

  // Release every blocked worker and wait for them all to finish.
  sys_atomic_set(&_release, 1);
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  // Now that every slot has been returned to the pool, dispatch must
  // succeed again. _release is already 1, so this worker won't block.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  test_assert(try_dispatch());
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  sys_exit();
  return 0;
}

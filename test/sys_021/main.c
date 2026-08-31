#include <picofuse/sys.h>
#include <test/test.h>

// Pico only has one spare core (core1); host platforms can spawn up to the
// full thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

#define NUM_ROUNDS 3

static sys_waitgroup_t *_wg;
static sys_atomic_t _round_counter;

static void worker(void *arg) {
  (void)arg;
  sys_atomic_inc(&_round_counter);
  test_assert(sys_waitgroup_done(_wg));
}

static void dispatch(void) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(worker, NULL, 1));
#else
  test_assert(sys_thread_create(worker, NULL));
#endif
}

test_main_sys(0) {

  // A single wait group handle is reused across every round below - no
  // deinit/reinit between add()/wait() cycles.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);

  for (int round = 0; round < NUM_ROUNDS; round++) {
    sys_atomic_init(&_round_counter, 0);

    test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

    for (int i = 0; i < NUM_WORKERS; i++) {
      dispatch();
    }

    sys_waitgroup_wait(_wg);

    // Each round must be fully independent: no leftover counter/waiter
    // state from a previous round should affect this one.
    test_assert(sys_atomic_get(&_round_counter) == (uint32_t)NUM_WORKERS);
  }

  sys_waitgroup_deinit(_wg);

}

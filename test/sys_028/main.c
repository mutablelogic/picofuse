#include <picofuse/sys.h>
#include <test/test.h>

// This test needs several genuinely simultaneous waiters distinct from the
// thread that issues signals - Pico only has one spare core (core1), so
// there's no way to have 2+ independent waiters blocked at once alongside
// the main thread driving the test. Not exercisable on a single-extra-core
// bare-metal target, so this test is a no-op there (same reasoning as
// test/sys_004).
#if !defined(SYSTEM_NAME_PICO)
#define NUM_WAITERS 5
#define NUM_SIGNALS 2
#define WAIT_TIMEOUT_MS 100

static sys_mutex_t *_mutex;
static sys_cond_t *_cond;
static sys_waitgroup_t *_wg;
static sys_atomic_t _signaled_count;
static sys_atomic_t _timed_out_count;

static void waiter(void *arg) {
  (void)arg;
  test_assert(sys_mutex_lock(_mutex));
  bool signaled = sys_cond_timedwait(_cond, _mutex, WAIT_TIMEOUT_MS);
  test_assert(sys_mutex_unlock(_mutex));

  if (signaled) {
    sys_atomic_inc(&_signaled_count);
  } else {
    sys_atomic_inc(&_timed_out_count);
  }
  test_assert(sys_waitgroup_done(_wg));
}
#endif

test_main_sys() {

#if !defined(SYSTEM_NAME_PICO)
  _mutex = sys_mutex_init();
  test_assert(_mutex != NULL);
  _cond = sys_cond_init();
  test_assert(_cond != NULL);

  sys_atomic_init(&_signaled_count, 0);
  sys_atomic_init(&_timed_out_count, 0);
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WAITERS));

  for (int i = 0; i < NUM_WAITERS; i++) {
    test_assert(sys_thread_create(waiter, NULL));
  }

  // Give every waiter a real chance to start blocking, then issue fewer
  // real signals than there are waiters, roughly midway through their
  // timeout window - some waiters consume a real signal, the rest
  // genuinely time out. This exercises the exact "more waiters than
  // signals" imbalance the pending-signal accounting exists to handle
  // correctly, including reclaiming a signal from a waiter that times out
  // right as it's being delivered.
  sys_sleep_ms(WAIT_TIMEOUT_MS / 2);
  for (int i = 0; i < NUM_SIGNALS; i++) {
    test_assert(sys_cond_signal(_cond));
  }

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  // Exactly as many waiters must have been genuinely signaled as there
  // were real signal() calls - no more (a phantom extra wakeup) and no
  // fewer (a lost signal).
  test_assert(sys_atomic_get(&_signaled_count) == NUM_SIGNALS);
  test_assert(sys_atomic_get(&_timed_out_count) == NUM_WAITERS - NUM_SIGNALS);

  // Probe: after that imbalanced round, the cond var must be back to a
  // clean slate - no leftover pending signal should let a fresh, unrelated
  // wait return early.
  test_assert(sys_mutex_lock(_mutex));
  uint64_t before = sys_timestamp_ms();
  bool probe_signaled = sys_cond_timedwait(_cond, _mutex, 150);
  uint64_t elapsed = sys_timestamp_ms() - before;
  test_assert(sys_mutex_unlock(_mutex));
  test_assert(!probe_signaled);
  test_assert(elapsed >= 100);

  sys_cond_deinit(_cond);
  sys_mutex_deinit(_mutex);
#endif

}

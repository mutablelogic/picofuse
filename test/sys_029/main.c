#include <picofuse/sys.h>
#include <test/test.h>

static sys_mutex_t *_mutex;
static sys_cond_t *_cond;
static sys_waitgroup_t *_wg;
static bool _ready;
static sys_atomic_t _result;

static void dispatch(sys_thread_func_t func) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, NULL, 1));
#else
  test_assert(sys_thread_create(func, NULL));
#endif
}

static void signal_after_delay(void *arg) {
  (void)arg;
  sys_sleep_ms(100);
  test_assert(sys_mutex_lock(_mutex));
  _ready = true;
  test_assert(sys_mutex_unlock(_mutex));
  test_assert(sys_cond_signal(_cond));
}

// No predicate here on purpose: this waiter is meant to still be genuinely
// blocked when the main thread calls sys_cond_deinit() on it. A 3s bound
// keeps a real bug (deinit's safety broadcast failing to release it)
// bounded and detectable instead of hanging the whole suite.
static void deinit_target_waiter(void *arg) {
  (void)arg;
  test_assert(sys_mutex_lock(_mutex));
  bool signaled = sys_cond_timedwait(_cond, _mutex, 3000);
  test_assert(sys_mutex_unlock(_mutex));
  sys_atomic_set(&_result, signaled ? 1 : 2);
  test_assert(sys_waitgroup_done(_wg));
}

test_main_sys(0) {

  // Part A: bare sys_cond_wait() (no timeout) actually blocks until
  // signaled - a distinct code path from timedwait's semaphore-with-
  // timeout call, never exercised anywhere so far.
  {
    _mutex = sys_mutex_init();
    test_assert(_mutex != NULL);
    _cond = sys_cond_init();
    test_assert(_cond != NULL);
    _ready = false;

    dispatch(signal_after_delay);

    test_assert(sys_mutex_lock(_mutex));
    while (!_ready) {
      test_assert(sys_cond_wait(_cond, _mutex));
    }
    test_assert(sys_mutex_unlock(_mutex));

    sys_cond_deinit(_cond);
    sys_mutex_deinit(_mutex);
  }

  // Part B: sys_cond_timedwait()'s return value and timing when genuinely
  // signaled well before the deadline - true, and quick.
  {
    _mutex = sys_mutex_init();
    test_assert(_mutex != NULL);
    _cond = sys_cond_init();
    test_assert(_cond != NULL);
    _ready = false;

    dispatch(signal_after_delay);

    test_assert(sys_mutex_lock(_mutex));
    uint64_t before = sys_timestamp_ms();
    bool got_signal = false;
    while (!_ready) {
      got_signal = sys_cond_timedwait(_cond, _mutex, 2000);
      test_assert(sys_timestamp_ms() - before < 5000);
    }
    uint64_t elapsed = sys_timestamp_ms() - before;
    test_assert(sys_mutex_unlock(_mutex));

    test_assert(got_signal);
    test_assert(elapsed < 2000);

    sys_cond_deinit(_cond);
    sys_mutex_deinit(_mutex);
  }

  // Part C: sys_cond_timedwait(cond, mutex, 0) must behave exactly like a
  // bare sys_cond_wait() - blocking indefinitely, not returning instantly.
  {
    _mutex = sys_mutex_init();
    test_assert(_mutex != NULL);
    _cond = sys_cond_init();
    test_assert(_cond != NULL);
    _ready = false;

    dispatch(signal_after_delay);

    test_assert(sys_mutex_lock(_mutex));
    while (!_ready) {
      test_assert(sys_cond_timedwait(_cond, _mutex, 0));
    }
    test_assert(sys_mutex_unlock(_mutex));

    sys_cond_deinit(_cond);
    sys_mutex_deinit(_mutex);
  }

  // Part D: sys_cond_deinit() must release a still-blocked waiter itself
  // via its documented internal safety broadcast, promptly - not leave it
  // to time out on its own.
  {
    _mutex = sys_mutex_init();
    test_assert(_mutex != NULL);
    _cond = sys_cond_init();
    test_assert(_cond != NULL);
    sys_atomic_init(&_result, 0);
    _wg = sys_waitgroup_init();
    test_assert(_wg != NULL);
    test_assert(sys_waitgroup_add(_wg, 1));

    dispatch(deinit_target_waiter);

    // Give the waiter a real chance to actually be blocked before deinit.
    sys_sleep_ms(100);

    uint64_t before = sys_timestamp_ms();
    sys_cond_deinit(_cond);

    sys_waitgroup_wait(_wg);
    sys_waitgroup_deinit(_wg);
    uint64_t elapsed = sys_timestamp_ms() - before;

    test_assert(sys_atomic_get(&_result) != 0);
    test_assert(elapsed < 500);

    sys_mutex_deinit(_mutex);
  }

  // Part E: exhaust the cond-variable pool, up to its compiled-in
  // SYS_COND_CAPACITY.
  {
    sys_cond_t *pool[SYS_COND_CAPACITY];
    for (int i = 0; i < SYS_COND_CAPACITY; i++) {
      pool[i] = sys_cond_init();
      test_assert(pool[i] != NULL);
    }

    test_assert(sys_cond_init() == NULL);

    for (int i = 0; i < SYS_COND_CAPACITY; i++) {
      sys_cond_deinit(pool[i]);
    }
  }

}

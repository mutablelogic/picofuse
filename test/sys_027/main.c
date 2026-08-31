#include <picofuse/sys.h>
#include <test/test.h>

// Pico only has one spare core (core1); host platforms can spawn up to the
// full thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WAITERS 1
#else
#define NUM_WAITERS SYS_THREAD_CAPACITY
#endif

static sys_mutex_t *_mutex;
static sys_cond_t *_cond;
static bool _ready;
static sys_waitgroup_t *_wg;
static sys_atomic_t _released_count;

static void waiter(void *arg) {
  (void)arg;
  test_assert(sys_mutex_lock(_mutex));
  uint64_t start = sys_timestamp_ms();
  while (!_ready) {
    sys_cond_timedwait(_cond, _mutex, 200);
    test_assert(sys_timestamp_ms() - start < 5000);
  }
  test_assert(sys_mutex_unlock(_mutex));

  sys_atomic_inc(&_released_count);
  test_assert(sys_waitgroup_done(_wg));
}

static void dispatch(sys_thread_func_t func) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, NULL, 1));
#else
  test_assert(sys_thread_create(func, NULL));
#endif
}

test_main_sys(0) {

  _mutex = sys_mutex_init();
  test_assert(_mutex != NULL);
  _cond = sys_cond_init();
  test_assert(_cond != NULL);

  // Part 1: sys_cond_broadcast() must wake every waiter, not just one.
  _ready = false;
  sys_atomic_init(&_released_count, 0);
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WAITERS));

  for (int i = 0; i < NUM_WAITERS; i++) {
    dispatch(waiter);
  }

  // Give every waiter a real head start to actually block before we
  // broadcast, so this genuinely exercises several threads blocked on the
  // same cond var at once - not just threads that happen to arrive late.
  sys_sleep_ms(150);

  test_assert(sys_mutex_lock(_mutex));
  _ready = true;
  test_assert(sys_mutex_unlock(_mutex));
  test_assert(sys_cond_broadcast(_cond));

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  test_assert(sys_atomic_get(&_released_count) == (uint32_t)NUM_WAITERS);

  // Part 2: sys_cond_signal()/broadcast() with nobody waiting must be a
  // true no-op - it must not "bank" a phantom wakeup that lets some LATER,
  // unrelated waiter return instantly without ever really being signaled.
  test_assert(sys_cond_signal(_cond));
  test_assert(sys_cond_broadcast(_cond));

  _ready = false;
  bool woke_unexpectedly;
  test_assert(sys_mutex_lock(_mutex));
  {
    uint64_t before = sys_timestamp_ms();
    bool signaled = sys_cond_timedwait(_cond, _mutex, 150);
    uint64_t elapsed = sys_timestamp_ms() - before;
    // A real timeout should take close to the full duration; returning
    // "signaled" at all, or returning almost immediately, would mean an
    // earlier no-op signal/broadcast was incorrectly banked.
    woke_unexpectedly = signaled || elapsed < 50;
  }
  test_assert(sys_mutex_unlock(_mutex));
  test_assert(!woke_unexpectedly);

  sys_cond_deinit(_cond);
  sys_mutex_deinit(_mutex);

}

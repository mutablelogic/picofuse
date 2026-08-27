#include <picofuse/sys.h>
#include <test/test.h>

static sys_mutex_t *_mutex;
static sys_waitgroup_t *_wg;
static sys_atomic_t _holder_ready;

static void holder(void *arg) {
  (void)arg;
  test_assert(sys_mutex_lock(_mutex));
  sys_atomic_set(&_holder_ready, 1);
  sys_sleep_ms(150);
  test_assert(sys_mutex_unlock(_mutex));
  test_assert(sys_waitgroup_done(_wg));
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

  _mutex = sys_mutex_init();
  test_assert(_mutex != NULL);

  // trylock succeeds on a free mutex.
  test_assert(sys_mutex_trylock(_mutex));

  // The same thread trying to lock a mutex it already holds must fail, not
  // deadlock or silently recurse - sys_mutex_t is non-recursive.
  test_assert(!sys_mutex_trylock(_mutex));

  test_assert(sys_mutex_unlock(_mutex));

  // trylock succeeds again once released.
  test_assert(sys_mutex_trylock(_mutex));
  test_assert(sys_mutex_unlock(_mutex));

  // Real cross-thread contention: trylock must fail while another thread
  // genuinely holds the mutex, then succeed again once it's released.
  sys_atomic_init(&_holder_ready, 0);
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));

  dispatch(holder);

  // Wait for the holder to actually acquire the lock before probing it.
  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&_holder_ready) == 0) {
    test_assert(sys_timestamp_ms() - start < 5000);
    sys_sleep_ms(5);
  }

  test_assert(!sys_mutex_trylock(_mutex));

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  // The holder unlocks before calling done(), so this must succeed now.
  test_assert(sys_mutex_trylock(_mutex));
  test_assert(sys_mutex_unlock(_mutex));

  sys_mutex_deinit(_mutex);

  sys_exit();
  return 0;
}

#include <picofuse/sys.h>
#include <test/test.h>

static sys_mutex_t *_mutex;
static sys_cond_t *_cond;
static bool _ready;
static int _value;

static void producer(void *arg) {
  (void)arg;
  // Give the consumer a real chance to actually be blocked in
  // sys_cond_wait() before we signal, rather than racing ahead and setting
  // the predicate before it ever starts waiting.
  sys_sleep_ms(100);

  test_assert(sys_mutex_lock(_mutex));
  _value = 42;
  _ready = true;
  test_assert(sys_mutex_unlock(_mutex));

  test_assert(sys_cond_signal(_cond));
}

static void dispatch(sys_thread_func_t func) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, NULL, 1));
#else
  test_assert(sys_thread_create(func, NULL));
#endif
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  _mutex = sys_mutex_init();
  test_assert(_mutex != NULL);
  _cond = sys_cond_init();
  test_assert(_cond != NULL);
  _ready = false;
  _value = 0;

  dispatch(producer);

  test_assert(sys_mutex_lock(_mutex));

  // The canonical predicate-guarded wait loop: re-check under the mutex
  // every time we wake, whether from a real signal or a timeout, so this
  // is correct regardless of whether the producer's signal arrives before
  // or after we start waiting. Using timedwait (rather than a bare wait)
  // means a genuinely broken signal/wait pairing fails this test cleanly
  // within a few seconds instead of hanging the whole suite forever.
  uint64_t start = sys_timestamp_ms();
  while (!_ready) {
    sys_cond_timedwait(_cond, _mutex, 200);
    test_assert(sys_timestamp_ms() - start < 5000);
  }

  test_assert(_value == 42);
  test_assert(sys_mutex_unlock(_mutex));

  sys_cond_deinit(_cond);
  sys_mutex_deinit(_mutex);

  sys_exit();
  return 0;
}

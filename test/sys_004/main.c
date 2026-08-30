#include <picofuse/sys.h>
#include <test/test.h>

// Real cross-thread contention: sys_mutex_lock() must actually block a
// second thread until the holder releases the mutex. Not exercisable on a
// single-core bare-metal target, so this test is a no-op there.
#if !defined(SYSTEM_NAME_PICO)
#include <pthread.h>
#include <time.h>

static sys_mutex_t *_contended_mutex;
static volatile bool _thread_acquired = false;

static void *_lock_thread(void *arg) {
  (void)arg;
  test_assert(sys_mutex_lock(_contended_mutex));
  _thread_acquired = true;
  test_assert(sys_mutex_unlock(_contended_mutex));
  return NULL;
}
#endif

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

#if !defined(SYSTEM_NAME_PICO)
  _contended_mutex = sys_mutex_init();
  test_assert(_contended_mutex != NULL);

  // Hold the mutex on the main thread, then spawn a thread that must block
  // in sys_mutex_lock() until we release it.
  test_assert(sys_mutex_lock(_contended_mutex));

  pthread_t thread;
  test_assert(pthread_create(&thread, NULL, _lock_thread, NULL) == 0);

  // Give the other thread a generous window to run and block on the lock.
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000};
  nanosleep(&delay, NULL);
  test_assert(!_thread_acquired);

  test_assert(sys_mutex_unlock(_contended_mutex));
  test_assert(pthread_join(thread, NULL) == 0);
  test_assert(_thread_acquired);

  sys_mutex_deinit(_contended_mutex);
#endif

  sys_exit();
  return 0;
}

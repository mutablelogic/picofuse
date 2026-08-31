#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  // sys_init() may already hold some pool slots for its own subsystems
  // (e.g. printf's mutex), so drain whatever's left rather than assuming a
  // pristine pool of exactly SYS_MUTEX_CAPACITY free slots.
  sys_mutex_t *mutexes[SYS_MUTEX_CAPACITY];
  size_t count = 0;
  while (count < SYS_MUTEX_CAPACITY) {
    sys_mutex_t *mutex = sys_mutex_init();
    if (mutex == NULL) {
      break;
    }
    mutexes[count++] = mutex;
  }
  test_assert(count > 1);

  // Locking one pool mutex must not affect any other independently
  // allocated mutex from the same pool.
  test_assert(sys_mutex_lock(mutexes[0]));
  test_assert(sys_mutex_trylock(mutexes[1]));
  test_assert(!sys_mutex_trylock(mutexes[0]));
  test_assert(sys_mutex_unlock(mutexes[1]));
  test_assert(sys_mutex_unlock(mutexes[0]));

  // The pool is now exhausted: one more allocation must fail.
  test_assert(sys_mutex_init() == NULL);

  for (size_t i = 0; i < count; i++) {
    sys_mutex_deinit(mutexes[i]);
  }

  // A freed slot should be available for reuse.
  sys_mutex_t *mutex = sys_mutex_init();
  test_assert(mutex != NULL);
  sys_mutex_deinit(mutex);

}

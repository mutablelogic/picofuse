#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  for (int i = 0; i < 50; i++) {
    sys_mutex_t *mutex = sys_mutex_init();
    test_assert(mutex != NULL);

    // trylock must succeed on a fresh, unlocked mutex.
    test_assert(sys_mutex_trylock(mutex));
    test_assert(sys_mutex_unlock(mutex));

    test_assert(sys_mutex_lock(mutex));
    test_assert(!sys_mutex_trylock(mutex));
    test_assert(sys_mutex_unlock(mutex));
    sys_mutex_deinit(mutex);
  }

}

#include <picofuse/sys.h>
#include <test/test.h>

static sys_atomic_t worker_ran;

static void worker(void *arg) {
  sys_waitgroup_t *wg = (sys_waitgroup_t *)arg;
  sys_atomic_set(&worker_ran, 1);
  test_assert(sys_waitgroup_done(wg));
}

test_main_sys(0) {
  sys_atomic_init(&worker_ran, 0);

  uint8_t numcores = sys_thread_numcores();
  test_assert(numcores >= 1);

  if (numcores > 1) {
    sys_waitgroup_t *wg = sys_waitgroup_init();
    test_assert(wg != NULL);
    test_assert(sys_waitgroup_add(wg, 1));

    // Pico requires an explicit target core (sys_thread_create always fails
    // there); host platforms use "any available core" since core-pinning
    // isn't supported everywhere (e.g. Darwin).
#if defined(SYSTEM_NAME_PICO)
    test_assert(sys_thread_create_on_core(worker, wg, 1));
#else
    test_assert(sys_thread_create(worker, wg));
#endif

    sys_waitgroup_wait(wg);
    test_assert(sys_atomic_get(&worker_ran) == 1);

    sys_waitgroup_deinit(wg);
  }

}

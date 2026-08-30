#include <picofuse/sys.h>
#include <test/test.h>

static sys_waitgroup_t *_wg;
static sys_atomic_t _worker_ran;
static sys_atomic_t _observed_core;

static void plain_worker(void *arg) {
  (void)arg;
  // "may be scheduled on any available core" makes no placement promise,
  // but the reported core must still be a valid, in-range value.
  uint8_t core = sys_thread_core();
  test_assert(core < sys_thread_numcores());
  sys_atomic_set(&_worker_ran, 1);
  test_assert(sys_waitgroup_done(_wg));
}

static void core_check_worker(void *arg) {
  uint8_t expected_core = (uint8_t)(uintptr_t)arg;
  uint8_t core = sys_thread_core();
  sys_atomic_set(&_observed_core, core);
  test_assert(core == expected_core);
  sys_atomic_set(&_worker_ran, 1);
  test_assert(sys_waitgroup_done(_wg));
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  uint8_t numcores = sys_thread_numcores();
  test_assert(numcores >= 1);

  // A NULL function must always be rejected, on both entry points.
  test_assert(!sys_thread_create(NULL, NULL));
  test_assert(!sys_thread_create_on_core(NULL, NULL, 0));

  // An out-of-range core index must be rejected.
  test_assert(!sys_thread_create_on_core(plain_worker, NULL, numcores));

  // sys_thread_create() ("any available core"): the dispatched thread must
  // report a valid, in-range core via sys_thread_core().
  sys_atomic_init(&_worker_ran, 0);
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  test_assert(sys_thread_create(plain_worker, NULL));
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);
  test_assert(sys_atomic_get(&_worker_ran) == 1);

  // sys_thread_create_on_core(): where core-pinning is actually supported,
  // the dispatched worker must genuinely report the requested core via
  // sys_thread_core(). Some platforms (e.g. Darwin) don't support pinning
  // at all and always return false here - that's a documented, acceptable
  // outcome ("false if core unavailable"), not a failure of this test.
  if (numcores > 1) {
    sys_atomic_init(&_worker_ran, 0);
    sys_atomic_init(&_observed_core, 0xFF);
    _wg = sys_waitgroup_init();
    test_assert(_wg != NULL);
    test_assert(sys_waitgroup_add(_wg, 1));

    if (sys_thread_create_on_core(core_check_worker, (void *)(uintptr_t)1,
                                  1)) {
      sys_waitgroup_wait(_wg);
      test_assert(sys_atomic_get(&_worker_ran) == 1);
      test_assert(sys_atomic_get(&_observed_core) == 1);
    } else {
      // Core pinning isn't supported on this platform; nothing was
      // dispatched, so there's nothing to wait for.
      test_assert(sys_atomic_get(&_worker_ran) == 0);
    }

    sys_waitgroup_deinit(_wg);
  }

  sys_exit();
  return 0;
}

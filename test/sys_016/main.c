#include <picofuse/sys.h>
#include <test/test.h>

// Number of increments each worker performs. Large enough that a genuinely
// non-atomic increment (a lost read-modify-write under real concurrency)
// would reliably under-count the final total, rather than getting lucky.
#define INCREMENTS_PER_WORKER 10000

// Pico only has one spare core (core1), and sys_thread_create_on_core is the
// only variant that works there; host platforms can spawn up to the full
// thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

static sys_atomic_t _counter;
static sys_waitgroup_t *_wg;

static void increment_many(void) {
  for (int i = 0; i < INCREMENTS_PER_WORKER; i++) {
    sys_atomic_inc(&_counter);
  }
}

static void worker(void *arg) {
  (void)arg;
  increment_many();
  test_assert(sys_waitgroup_done(_wg));
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);
  sys_atomic_init(&_counter, 0);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);

  // add() must happen before any worker is dispatched, so the counter can
  // never touch zero (and release a waiter) before every worker is counted.
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  for (int i = 0; i < NUM_WORKERS; i++) {
#if defined(SYSTEM_NAME_PICO)
    test_assert(sys_thread_create_on_core(worker, NULL, 1));
#else
    test_assert(sys_thread_create(worker, NULL));
#endif
  }

  // The launching thread races the workers on the same counter too, so this
  // exercises the atomic across every core the platform actually has, not
  // just the spawned ones.
  increment_many();

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  uint32_t expected = (uint32_t)(NUM_WORKERS + 1) * INCREMENTS_PER_WORKER;
  test_assert(sys_atomic_get(&_counter) == expected);

  sys_exit();
  return 0;
}

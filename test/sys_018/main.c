#include <picofuse/sys.h>
#include <test/test.h>

// Number of increments each worker performs. Large enough that a genuine
// mutual-exclusion failure (a lost, unsynchronized read-modify-write on the
// plain counter) would reliably under-count the final total.
#define INCREMENTS_PER_WORKER 10000

// Pico only has one spare core (core1), and sys_thread_create_on_core is the
// only variant that works there; host platforms can spawn up to the full
// thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

static sys_mutex_t *_mutex;
static int _counter;
static sys_waitgroup_t *_wg;

// The counter itself is a plain (non-atomic) int - correctness here depends
// entirely on sys_mutex_lock/unlock actually providing mutual exclusion,
// not on the counter update being atomic on its own.
static void increment_many(void) {
  for (int i = 0; i < INCREMENTS_PER_WORKER; i++) {
    test_assert(sys_mutex_lock(_mutex));
    _counter++;
    test_assert(sys_mutex_unlock(_mutex));
  }
}

static void worker(void *arg) {
  (void)arg;
  increment_many();
  test_assert(sys_waitgroup_done(_wg));
}

int main(void) {
  sys_init();

  _mutex = sys_mutex_init();
  test_assert(_mutex != NULL);
  _counter = 0;

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

  // The launching thread contends for the same mutex-protected counter too,
  // so this exercises real contention across every core the platform
  // actually has, not just the spawned ones.
  increment_many();

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  int expected = (NUM_WORKERS + 1) * INCREMENTS_PER_WORKER;
  test_assert(_counter == expected);

  sys_mutex_deinit(_mutex);

  sys_exit();
  return 0;
}

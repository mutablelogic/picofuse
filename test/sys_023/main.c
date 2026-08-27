#include <picofuse/sys.h>
#include <test/test.h>

// Pico only has one spare core (core1); host platforms can spawn up to the
// full thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

#define OPS_PER_PARTICIPANT 500
#define TOTAL_OPS ((NUM_WORKERS + 1) * OPS_PER_PARTICIPANT)

static sys_atomic_t _counter;
static sys_waitgroup_t *_wg;
static bool _inc_seen[TOTAL_OPS];
static bool _dec_seen[TOTAL_OPS];

static void inc_worker(void *arg) {
  (void)arg;
  for (int i = 0; i < OPS_PER_PARTICIPANT; i++) {
    uint32_t value = sys_atomic_inc(&_counter);
    test_assert(value >= 1 && value <= TOTAL_OPS);
    _inc_seen[value - 1] = true;
  }
  test_assert(sys_waitgroup_done(_wg));
}

static void dec_worker(void *arg) {
  (void)arg;
  for (int i = 0; i < OPS_PER_PARTICIPANT; i++) {
    uint32_t value = sys_atomic_dec(&_counter);
    test_assert(value < TOTAL_OPS);
    _dec_seen[value] = true;
  }
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

  // Single-threaded sanity: inc/dec return the post-operation value, not
  // the pre-operation one.
  sys_atomic_t solo;
  sys_atomic_init(&solo, 5);
  test_assert(sys_atomic_inc(&solo) == 6);
  test_assert(sys_atomic_get(&solo) == 6);
  test_assert(sys_atomic_dec(&solo) == 5);
  test_assert(sys_atomic_get(&solo) == 5);

  // Concurrent: every sys_atomic_inc() call across every participant must
  // return a distinct value. If two increments ever raced and returned the
  // same "new value", some other value in [1, TOTAL_OPS] would never be
  // produced by anyone, leaving a gap this test can detect - a check the
  // final-total-only verification in sys_016 can't catch.
  sys_atomic_init(&_counter, 0);
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  for (int i = 0; i < NUM_WORKERS; i++) {
    dispatch(inc_worker);
  }
  for (int i = 0; i < OPS_PER_PARTICIPANT; i++) {
    uint32_t value = sys_atomic_inc(&_counter);
    test_assert(value >= 1 && value <= TOTAL_OPS);
    _inc_seen[value - 1] = true;
  }

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  for (int i = 0; i < TOTAL_OPS; i++) {
    test_assert(_inc_seen[i]);
  }
  test_assert(sys_atomic_get(&_counter) == (uint32_t)TOTAL_OPS);

  // Same check for sys_atomic_dec(), counting back down to zero.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  for (int i = 0; i < NUM_WORKERS; i++) {
    dispatch(dec_worker);
  }
  for (int i = 0; i < OPS_PER_PARTICIPANT; i++) {
    uint32_t value = sys_atomic_dec(&_counter);
    test_assert(value < TOTAL_OPS);
    _dec_seen[value] = true;
  }

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  for (int i = 0; i < TOTAL_OPS; i++) {
    test_assert(_dec_seen[i]);
  }
  test_assert(sys_atomic_get(&_counter) == 0);

  sys_exit();
  return 0;
}

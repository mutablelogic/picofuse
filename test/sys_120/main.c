#include <picofuse/sys.h>
#include <test/test.h>

// clang warns that atomics aren't lock-free on an RP2040
#if defined(__clang__)
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

#define TIMES 10000

// Pico has one spare core; hosts spawn as many threads as they're allowed
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

static sys_atomic_t _counter, _lock;
static sys_waitgroup_t *_wg;
static volatile uint32_t _guarded; // only changed while _lock is 1

// Adds one with a compare-and-swap loop
static void add_with_swap(void) {
  uint32_t value = sys_atomic_get(&_counter);
  while (!sys_atomic_compare_swap(&_counter, &value, value + 1)) {
    // value is now what it was, so try again
  }
}

// A lock made from compare-and-swap: 0 is free, 1 is held
static void with_lock(void) {
  uint32_t free = 0;
  while (!sys_atomic_compare_swap(&_lock, &free, 1)) {
    free = 0;
  }
  _guarded = _guarded + 1;
  sys_atomic_set(&_lock, 0);
}

static void work(void) {
  for (int i = 0; i < TIMES; i++) {
    add_with_swap();
    with_lock();
  }
}

static void worker(void *arg) {
  (void)arg;
  work();
  test_assert(sys_waitgroup_done(_wg));
}

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Swaps when the value is expected, and otherwise says what it is

  {
    sys_atomic_t a;
    sys_atomic_init(&a, 5);
    uint32_t expected = 5;
    test_assert(sys_atomic_compare_swap(&a, &expected, 7));
    test_assert(expected == 5 && sys_atomic_get(&a) == 7);
    expected = 5;
    test_assert(!sys_atomic_compare_swap(&a, &expected, 9));
    test_assert(expected == 7 && sys_atomic_get(&a) == 7);
    expected = 7;
    test_assert(sys_atomic_compare_swap(&a, &expected, UINT32_MAX));
    test_assert(sys_atomic_get(&a) == UINT32_MAX);
    expected = UINT32_MAX;
    test_assert(sys_atomic_compare_swap(&a, &expected, UINT32_MAX));
    test_assert(sys_atomic_get(&a) == UINT32_MAX);
  }

  ///////////////////////////////////////////////////////////////////////
  // From several threads or cores at once: none are lost, and the lock
  // keeps the guarded count right

  sys_atomic_init(&_counter, 0);
  sys_atomic_init(&_lock, 0);
  _guarded = 0;
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));
  for (int i = 0; i < NUM_WORKERS; i++) {
#if defined(SYSTEM_NAME_PICO)
    test_assert(sys_thread_create_on_core(worker, NULL, 1));
#else
    test_assert(sys_thread_create(worker, NULL));
#endif
  }
  work();
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  uint32_t expected = (uint32_t)(NUM_WORKERS + 1) * TIMES;
  test_assert(sys_atomic_get(&_counter) == expected);
  test_assert(_guarded == expected);
}

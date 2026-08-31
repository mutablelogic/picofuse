#include <picofuse/sys.h>
#include <test/test.h>

// Pico only has one spare core (core1); host platforms can spawn up to the
// full thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

// One extra bit for the launching thread itself, so this exercises real
// cross-core concurrency even on Pico, which only has one spare core.
#define NUM_BITS (NUM_WORKERS + 1)

static sys_atomic_t _flags;
static sys_waitgroup_t *_wg;

static void set_worker(void *arg) {
  int index = (int)(uintptr_t)arg;
  sys_atomic_set_bits(&_flags, 1u << index);
  test_assert(sys_waitgroup_done(_wg));
}

static void clear_worker(void *arg) {
  int index = (int)(uintptr_t)arg;
  sys_atomic_clear_bits(&_flags, 1u << index);
  test_assert(sys_waitgroup_done(_wg));
}

static void dispatch(sys_thread_func_t func, int index) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, (void *)(uintptr_t)index, 1));
#else
  test_assert(sys_thread_create(func, (void *)(uintptr_t)index));
#endif
}

test_main_sys(0) {
  sys_atomic_init(&_flags, 0);

  uint32_t full_mask =
      (NUM_BITS >= 32) ? 0xFFFFFFFFu : ((1u << NUM_BITS) - 1);

  // Phase 1: every participant sets its own distinct bit concurrently. A
  // non-atomic fetch-or would lose bits when two threads race on the same
  // word - this only passes if every single bit survives.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  for (int i = 0; i < NUM_WORKERS; i++) {
    dispatch(set_worker, i);
  }

  // The launching thread claims the last bit and races the workers too.
  sys_atomic_set_bits(&_flags, 1u << NUM_WORKERS);

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  test_assert(sys_atomic_get(&_flags) == full_mask);

  // Phase 2: every participant clears its own distinct bit concurrently.
  // A non-atomic fetch-and here would similarly leave a bit stuck set.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  for (int i = 0; i < NUM_WORKERS; i++) {
    dispatch(clear_worker, i);
  }

  sys_atomic_clear_bits(&_flags, 1u << NUM_WORKERS);

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  test_assert(sys_atomic_get(&_flags) == 0);

}

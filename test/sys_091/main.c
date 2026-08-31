#include <picofuse/sys.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <test/test.h>

// Pico only has one spare core (core1); host platforms can spawn up to the
// full thread pool via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_WORKERS 1
#else
#define NUM_WORKERS SYS_THREAD_CAPACITY
#endif

#define ITERATIONS_PER_WORKER 50

static sys_mem_arena_t *_arena;
static sys_waitgroup_t *_wg;
static sys_atomic_t _failures;

static void worker(void *arg) {
  unsigned char pattern = (unsigned char)(uintptr_t)arg;
  for (int i = 0; i < ITERATIONS_PER_WORKER; i++) {
    void *p = sys_mem_arena_alloc(_arena, 32);
    if (p == NULL) {
      sys_atomic_inc(&_failures);
      continue;
    }
    memset(p, pattern, 32);
    sys_sleep_ms(0); // encourage interleaving with other workers
    for (int j = 0; j < 32; j++) {
      if (((unsigned char *)p)[j] != pattern) {
        // Another worker's allocation must have overlapped ours - the
        // shared arena's locking isn't actually serializing access.
        sys_atomic_inc(&_failures);
        break;
      }
    }
    sys_mem_arena_free(_arena, p);
  }
  test_assert(sys_waitgroup_done(_wg));
}

static void dispatch(sys_thread_func_t func, void *arg) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, arg, 1));
#else
  test_assert(sys_thread_create(func, arg));
#endif
}

test_main_sys(0) {

  // Many threads/cores hammering the same arena concurrently must never
  // corrupt its bookkeeping or hand out overlapping memory - this is the
  // one thing sys_mutex_t is there to guarantee.

  _arena = sys_mem_arena_init(32u * 1024u, NULL, malloc, free);
  test_assert(_arena != NULL);
  sys_atomic_init(&_failures, 0);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, NUM_WORKERS));

  for (int i = 0; i < NUM_WORKERS; i++) {
    dispatch(worker, (void *)(uintptr_t)(0x10 + i));
  }

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  test_assert(sys_atomic_get(&_failures) == 0);

  // Every allocation was freed - a correctly-serialized arena has nothing
  // left outstanding.
  sys_mem_arena_stats_t stats;
  sys_mem_arena_next(_arena, &stats);
  test_assert(stats.allocations == 0);
  test_assert(stats.used_bytes == 0);

  sys_mem_arena_delete(_arena);

}

#include <picofuse/sys.h>
#include <stdint.h>
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

static sys_waitgroup_t *_wg;
static sys_atomic_t _failures;

static void worker(void *arg) {
  unsigned char pattern = (unsigned char)(uintptr_t)arg;

  for (int i = 0; i < ITERATIONS_PER_WORKER; i++) {
    // Varying, growing sizes deliberately push the shared default chain to
    // grow repeatedly - concurrently, from multiple threads/cores - which
    // is the one place this layer adds real logic beyond sys_mem_arena_t
    // itself (see _sys_mem_default_alloc()'s retry loop in mem.c).
    size_t size = 16 + (size_t)(i % 64);

    void *p = sys_malloc(size);
    if (p == NULL) {
      sys_atomic_inc(&_failures);
      continue;
    }
    memset(p, pattern, size);
    sys_sleep_ms(0); // encourage interleaving with other workers

    for (size_t j = 0; j < size; j++) {
      if (((unsigned char *)p)[j] != pattern) {
        // Another worker's allocation must have overlapped ours.
        sys_atomic_inc(&_failures);
        break;
      }
    }

    void *grown = sys_realloc(p, size * 2);
    if (grown == NULL) {
      sys_atomic_inc(&_failures);
      continue;
    }
    for (size_t j = 0; j < size; j++) {
      if (((unsigned char *)grown)[j] != pattern) {
        sys_atomic_inc(&_failures);
        break;
      }
    }

    void *zeroed = sys_calloc(4, size);
    if (zeroed == NULL) {
      sys_atomic_inc(&_failures);
    } else {
      for (size_t j = 0; j < 4 * size; j++) {
        if (((unsigned char *)zeroed)[j] != 0) {
          sys_atomic_inc(&_failures);
          break;
        }
      }
      sys_free(zeroed);
    }

    sys_free(grown);
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

// A small initial arena forces real concurrent growth under load, rather
// than everyone fitting comfortably in the first arena.
test_main_sys(4096) {

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

}

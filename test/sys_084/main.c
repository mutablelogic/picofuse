#include <picofuse/sys.h>
#include <stdlib.h>
#include <test/test.h>

static void *noop_malloc(size_t size) {
  (void)size;
  return NULL;
}
static void noop_free(void *ptr) { (void)ptr; }

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////////
  // First arena in a chain (prev == NULL): malloc_fn/free_fn required,
  // size must be nonzero.

  test_assert(sys_mem_arena_init(0, NULL, malloc, free) == NULL);
  test_assert(sys_mem_arena_init(1024, NULL, NULL, free) == NULL);
  test_assert(sys_mem_arena_init(1024, NULL, malloc, NULL) == NULL);
  test_assert(sys_mem_arena_init(1024, NULL, NULL, NULL) == NULL);

  sys_mem_arena_t *a = sys_mem_arena_init(1024, NULL, malloc, free);
  test_assert(a != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // Appending to a chain (prev != NULL): malloc_fn/free_fn must both be
  // NULL - they're inherited from prev, not re-specified.

  test_assert(sys_mem_arena_init(1024, a, malloc, NULL) == NULL);
  test_assert(sys_mem_arena_init(1024, a, NULL, free) == NULL);
  test_assert(sys_mem_arena_init(1024, a, malloc, free) == NULL);

  // A rejected append must not have mutated the chain: `a` is still the tail.
  test_assert(sys_mem_arena_next(a, NULL) == NULL);

  sys_mem_arena_t *b = sys_mem_arena_init(1024, a, NULL, NULL);
  test_assert(b != NULL);
  test_assert(sys_mem_arena_next(a, NULL) == b);

  // `prev` must be the current tail - `a` no longer is, now that `b` exists.
  test_assert(sys_mem_arena_init(1024, a, NULL, NULL) == NULL);

  ///////////////////////////////////////////////////////////////////////////
  // A failing underlying allocator surfaces as init failure, not a crash,
  // and doesn't leave anything linked into the chain.

  sys_mem_arena_t *c = sys_mem_arena_init(1024, NULL, noop_malloc, noop_free);
  test_assert(c == NULL);

  sys_mem_arena_delete(b);
  sys_mem_arena_delete(a);

}

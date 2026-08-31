#include <picofuse/sys.h>
#include <stdlib.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////////
  // Deleting the tail of a chain shrinks it by exactly one, and the arena
  // before it becomes the new (delete-able) tail.

  sys_mem_arena_t *a = sys_mem_arena_init(256, NULL, malloc, free);
  test_assert(a != NULL);
  sys_mem_arena_t *b = sys_mem_arena_init(256, a, NULL, NULL);
  test_assert(b != NULL);
  sys_mem_arena_t *c = sys_mem_arena_init(256, b, NULL, NULL);
  test_assert(c != NULL);

  sys_mem_arena_delete(c);
  test_assert(sys_mem_arena_next(b, NULL) == NULL); // b is the tail again

  sys_mem_arena_delete(b);
  test_assert(sys_mem_arena_next(a, NULL) == NULL); // a is the tail again

  ///////////////////////////////////////////////////////////////////////////
  // Deleting the sole remaining arena (both head and tail) tears down the
  // whole chain, including the shared mutex - the API surface is quiet
  // about this beyond "the pointer becomes invalid", so just confirm it
  // doesn't crash and the chain can start fresh afterward.

  sys_mem_arena_delete(a);

  sys_mem_arena_t *fresh = sys_mem_arena_init(256, NULL, malloc, free);
  test_assert(fresh != NULL);
  void *p = sys_mem_arena_alloc(fresh, 16);
  test_assert(p != NULL);
  sys_mem_arena_delete(fresh);

  ///////////////////////////////////////////////////////////////////////////
  // sys_mem_arena_delete(NULL) is a documented no-op.

  sys_mem_arena_delete(NULL);

}

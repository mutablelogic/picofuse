#include <picofuse/sys.h>
#include <stdlib.h>
#include <string.h>
#include <test/test.h>

test_main_sys(0) {

  sys_mem_arena_t *a = sys_mem_arena_init(4096, NULL, malloc, free);
  test_assert(a != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // realloc(NULL, n) behaves like alloc(); realloc(ptr, 0) behaves like
  // free() and returns NULL.

  void *p = sys_mem_arena_realloc(a, NULL, 32);
  test_assert(p != NULL);

  void *freed = sys_mem_arena_realloc(a, p, 0);
  test_assert(freed == NULL);

  sys_mem_arena_stats_t stats;
  sys_mem_arena_next(a, &stats);
  test_assert(stats.allocations == 0);

  ///////////////////////////////////////////////////////////////////////////
  // Shrinking is always in place - same pointer back, and the freed tail
  // becomes reusable.

  void *block = sys_mem_arena_alloc(a, 64);
  test_assert(block != NULL);
  memset(block, 0x42, 64);

  void *shrunk = sys_mem_arena_realloc(a, block, 8);
  test_assert(shrunk == block);
  test_assert(((unsigned char *)shrunk)[0] == 0x42); // surviving bytes preserved

  ///////////////////////////////////////////////////////////////////////////
  // Growing back within the original footprint is also in place.

  void *regrown = sys_mem_arena_realloc(a, shrunk, 64);
  test_assert(regrown == shrunk);

  ///////////////////////////////////////////////////////////////////////////
  // Growing beyond what's available in place must move: a new pointer,
  // with the original content preserved and the old block released.

  void *first = sys_mem_arena_alloc(a, 32);
  void *second = sys_mem_arena_alloc(a, 32); // occupies the gap right after `first`
  test_assert(first != NULL && second != NULL);
  memset(first, 0x77, 32);

  void *moved = sys_mem_arena_realloc(a, first, 2000); // far larger than any adjacent gap
  test_assert(moved != NULL);
  test_assert(moved != first);
  for (int i = 0; i < 32; i++) {
    test_assert(((unsigned char *)moved)[i] == 0x77);
  }

  // The old `first` block was released - a same-sized allocation reuses it.
  void *reused = sys_mem_arena_alloc(a, 32);
  test_assert(reused == first);

  ///////////////////////////////////////////////////////////////////////////
  // Growing past what the arena can ever hold fails cleanly, leaving the
  // original allocation completely untouched and still valid.

  void *small = sys_mem_arena_alloc(a, 16);
  test_assert(small != NULL);
  memset(small, 0x99, 16);

  void *too_big = sys_mem_arena_realloc(a, small, 1u << 20);
  test_assert(too_big == NULL);
  test_assert(((unsigned char *)small)[0] == 0x99); // untouched by the failed attempt
  test_assert(((unsigned char *)small)[15] == 0x99);

  sys_mem_arena_delete(a);

}

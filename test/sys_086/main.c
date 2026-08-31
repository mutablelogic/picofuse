#include <picofuse/sys.h>
#include <stdlib.h>
#include <test/test.h>

test_main_sys(0) {

  sys_mem_arena_t *a = sys_mem_arena_init(4096, NULL, malloc, free);
  test_assert(a != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // Freeing a middle allocation opens a reusable gap between its still-live
  // neighbors - a same-or-smaller-sized allocation afterward reuses exactly
  // that gap (first-fit, address order) rather than growing past the tail.

  void *block_a = sys_mem_arena_alloc(a, 64);
  void *block_b = sys_mem_arena_alloc(a, 64);
  void *block_c = sys_mem_arena_alloc(a, 64);
  test_assert(block_a != NULL && block_b != NULL && block_c != NULL);

  sys_mem_stats_t before;
  sys_mem_arena_next(a, &before);

  sys_mem_arena_free(a, block_b);

  sys_mem_stats_t after_free;
  sys_mem_arena_next(a, &after_free);
  test_assert(after_free.allocations == before.allocations - 1);
  test_assert(after_free.used_bytes == before.used_bytes - 64);

  void *block_d = sys_mem_arena_alloc(a, 64);
  test_assert(block_d != NULL);
  test_assert(block_d == block_b); // reused the exact gap block_b left behind

  sys_mem_stats_t after_reuse;
  sys_mem_arena_next(a, &after_reuse);
  test_assert(after_reuse.allocations == before.allocations);
  test_assert(after_reuse.used_bytes == before.used_bytes);

  ///////////////////////////////////////////////////////////////////////////
  // A smaller allocation than the freed gap also reuses it, just leaving a
  // smaller residual gap behind rather than failing or over-consuming space.

  sys_mem_arena_free(a, block_d);
  void *small = sys_mem_arena_alloc(a, 8);
  test_assert(small != NULL);
  test_assert(small == block_b);

  ///////////////////////////////////////////////////////////////////////////
  // Freeing everything and reallocating the same total footprint succeeds -
  // repeated free/alloc cycles don't leak capacity.

  sys_mem_arena_free(a, block_a);
  sys_mem_arena_free(a, small);
  sys_mem_arena_free(a, block_c);

  sys_mem_stats_t empty;
  sys_mem_arena_next(a, &empty);
  test_assert(empty.allocations == 0);
  test_assert(empty.used_bytes == 0);

  for (int i = 0; i < 10; i++) {
    void *p = sys_mem_arena_alloc(a, 64);
    test_assert(p != NULL);
    sys_mem_arena_free(a, p);
  }
  sys_mem_stats_t after_cycles;
  sys_mem_arena_next(a, &after_cycles);
  test_assert(after_cycles.allocations == 0);
  test_assert(after_cycles.used_bytes == 0);

  sys_mem_arena_delete(a);

}

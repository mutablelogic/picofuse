#include <picofuse/sys.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <test/test.h>

test_main_sys(0) {

  sys_mem_arena_t *a = sys_mem_arena_init(4096, NULL, malloc, free);
  test_assert(a != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // A fresh arena reports zero usage and a payload capacity that's at least
  // (but not necessarily exactly) the requested size, due to alignment
  // rounding.

  sys_mem_stats_t stats;
  sys_mem_arena_t *next = sys_mem_arena_next(a, &stats);
  test_assert(next == NULL);
  test_assert(stats.size_bytes >= 4096);
  test_assert(stats.used_bytes == 0);
  test_assert(stats.allocations == 0);

  ///////////////////////////////////////////////////////////////////////////
  // Every allocation, regardless of requested size, is aligned suitably for
  // any object type.

  size_t odd_sizes[] = {1, 3, 7, 13, 31, 63, 100};
  void *ptrs[7];
  for (size_t i = 0; i < 7; i++) {
    ptrs[i] = sys_mem_arena_alloc(a, odd_sizes[i]);
    test_assert(ptrs[i] != NULL);
    test_assert(((uintptr_t)ptrs[i] % _Alignof(max_align_t)) == 0);
  }

  sys_mem_arena_next(a, &stats);
  test_assert(stats.allocations == 7);
  test_assert(stats.used_bytes >= 1 + 3 + 7 + 13 + 31 + 63 + 100);

  // Every allocation is a genuinely distinct, independently writable region.
  for (size_t i = 0; i < 7; i++) {
    memset(ptrs[i], (int)(0x11 * (i + 1)), odd_sizes[i]);
  }
  for (size_t i = 0; i < 7; i++) {
    for (size_t j = 0; j < odd_sizes[i]; j++) {
      test_assert(((unsigned char *)ptrs[i])[j] == (unsigned char)(0x11 * (i + 1)));
    }
  }

  ///////////////////////////////////////////////////////////////////////////
  // Freeing decrements both used_bytes and allocations.

  for (size_t i = 0; i < 7; i++) {
    sys_mem_arena_free(a, ptrs[i]);
  }
  sys_mem_arena_next(a, &stats);
  test_assert(stats.used_bytes == 0);
  test_assert(stats.allocations == 0);

  // Freeing NULL is a documented no-op.
  sys_mem_arena_free(a, NULL);
  sys_mem_arena_next(a, &stats);
  test_assert(stats.used_bytes == 0);
  test_assert(stats.allocations == 0);

  sys_mem_arena_delete(a);

}

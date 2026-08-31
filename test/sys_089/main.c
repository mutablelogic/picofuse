#include <picofuse/sys.h>
#include <stdlib.h>
#include <test/test.h>

test_main_sys(0) {

  sys_mem_arena_t *a = sys_mem_arena_init(256, NULL, malloc, free);
  test_assert(a != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // Appending grows the chain; sys_mem_arena_next() walks it in append
  // order and ends in NULL past the tail.

  sys_mem_arena_t *b = sys_mem_arena_init(256, a, NULL, NULL);
  test_assert(b != NULL);
  test_assert(b != a);

  sys_mem_arena_t *c = sys_mem_arena_init(256, b, NULL, NULL);
  test_assert(c != NULL);

  test_assert(sys_mem_arena_next(a, NULL) == b);
  test_assert(sys_mem_arena_next(b, NULL) == c);
  test_assert(sys_mem_arena_next(c, NULL) == NULL);

  ///////////////////////////////////////////////////////////////////////////
  // `prev` must be the chain's current tail - anything else is rejected,
  // and rejection doesn't disturb the existing chain.

  test_assert(sys_mem_arena_init(256, a, NULL, NULL) == NULL); // a is no longer tail
  test_assert(sys_mem_arena_init(256, b, NULL, NULL) == NULL); // neither is b
  test_assert(sys_mem_arena_next(b, NULL) == c); // unchanged

  ///////////////////////////////////////////////////////////////////////////
  // Each arena's allocation/free calls operate only on that arena - filling
  // one completely never spills over into a successor, even though the
  // chain as a whole still has room.

  int allocated_in_a = 0;
  for (;;) {
    void *p = sys_mem_arena_alloc(a, 16);
    if (p == NULL) {
      break;
    }
    allocated_in_a++;
    test_assert(allocated_in_a < 1000); // sanity bound, not a real limit
  }
  test_assert(allocated_in_a > 0);
  test_assert(sys_mem_arena_alloc(a, 16) == NULL);

  // `b` and `c` are untouched - both still fully available.
  void *pb = sys_mem_arena_alloc(b, 16);
  void *pc = sys_mem_arena_alloc(c, 16);
  test_assert(pb != NULL);
  test_assert(pc != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // Independent arenas (no shared prev) have independent locks and stats -
  // exhausting one doesn't affect an unrelated arena's own bookkeeping.

  sys_mem_arena_t *independent = sys_mem_arena_init(64, NULL, malloc, free);
  test_assert(independent != NULL);
  void *pi = sys_mem_arena_alloc(independent, 16);
  test_assert(pi != NULL);
  test_assert(sys_mem_arena_next(independent, NULL) == NULL);

  sys_mem_arena_free(independent, pi);
  sys_mem_arena_delete(independent);

  sys_mem_arena_delete(c);
  sys_mem_arena_delete(b);
  sys_mem_arena_delete(a);

}

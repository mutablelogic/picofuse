#include <picofuse/sys.h>
#include <stdlib.h>
#include <test/test.h>

#define SYS_087_MAX_BLOCKS 256

test_main_sys(0) {

  sys_mem_arena_t *a = sys_mem_arena_init(256, NULL, malloc, free);
  test_assert(a != NULL);

  ///////////////////////////////////////////////////////////////////////////
  // A small arena eventually refuses further allocations rather than
  // overrunning its buffer - fill it to exhaustion.

  void *blocks[SYS_087_MAX_BLOCKS];
  int count = 0;
  while (count < SYS_087_MAX_BLOCKS) {
    void *p = sys_mem_arena_alloc(a, 16);
    if (p == NULL) {
      break;
    }
    blocks[count++] = p;
  }
  test_assert(count > 0);
  test_assert(count < SYS_087_MAX_BLOCKS); // genuinely exhausted, not just looped out

  test_assert(sys_mem_arena_alloc(a, 16) == NULL);
  // A too-large request against the *original* arena size also fails.
  test_assert(sys_mem_arena_alloc(a, 1u << 20) == NULL);

  ///////////////////////////////////////////////////////////////////////////
  // Freeing one block makes room for exactly one more allocation of the
  // same size.

  sys_mem_arena_free(a, blocks[count / 2]);
  void *reused = sys_mem_arena_alloc(a, 16);
  test_assert(reused != NULL);
  blocks[count / 2] = reused;

  test_assert(sys_mem_arena_alloc(a, 16) == NULL);

  for (int i = 0; i < count; i++) {
    sys_mem_arena_free(a, blocks[i]);
  }

  sys_mem_arena_stats_t stats;
  sys_mem_arena_next(a, &stats);
  test_assert(stats.allocations == 0);
  test_assert(stats.used_bytes == 0);

  // Fully drained, the arena can be filled again from scratch.
  void *p = sys_mem_arena_alloc(a, 16);
  test_assert(p != NULL);
  sys_mem_arena_free(a, p);

  sys_mem_arena_delete(a);

}

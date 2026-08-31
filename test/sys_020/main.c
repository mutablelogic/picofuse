#include <limits.h>
#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  // sys_waitgroup_add() rejects negative deltas.
  {
    sys_waitgroup_t *wg = sys_waitgroup_init();
    test_assert(wg != NULL);
    test_assert(!sys_waitgroup_add(wg, -1));

    // The rejected add must not have touched the counter: a normal
    // add/done cycle should still behave exactly as if it never happened.
    test_assert(sys_waitgroup_add(wg, 1));
    test_assert(sys_waitgroup_done(wg));

    sys_waitgroup_deinit(wg);
  }

  // sys_waitgroup_done() with no outstanding add()s fails gracefully
  // instead of driving the counter negative.
  {
    sys_waitgroup_t *wg = sys_waitgroup_init();
    test_assert(wg != NULL);
    test_assert(sys_waitgroup_add(wg, 1));
    test_assert(sys_waitgroup_done(wg));
    test_assert(!sys_waitgroup_done(wg));

    sys_waitgroup_deinit(wg);
  }

  // sys_waitgroup_add() rejects a delta that would overflow the counter.
  {
    sys_waitgroup_t *wg = sys_waitgroup_init();
    test_assert(wg != NULL);
    test_assert(sys_waitgroup_add(wg, INT_MAX));
    test_assert(!sys_waitgroup_add(wg, 1));

    sys_waitgroup_deinit(wg);
  }

  // sys_waitgroup_wait() on an already-zero counter returns immediately,
  // without blocking.
  {
    sys_waitgroup_t *wg = sys_waitgroup_init();
    test_assert(wg != NULL);

    uint64_t before = sys_timestamp_ms();
    sys_waitgroup_wait(wg);
    uint64_t elapsed = sys_timestamp_ms() - before;
    test_assert(elapsed < 50);

    sys_waitgroup_deinit(wg);
  }

  // Exhaust the wait-group pool, up to its compiled-in SYS_WAITGROUP_CAPACITY.
  {
    sys_waitgroup_t *pool[SYS_WAITGROUP_CAPACITY];
    for (int i = 0; i < SYS_WAITGROUP_CAPACITY; i++) {
      pool[i] = sys_waitgroup_init();
      test_assert(pool[i] != NULL);
    }

    test_assert(sys_waitgroup_init() == NULL);

    for (int i = 0; i < SYS_WAITGROUP_CAPACITY; i++) {
      sys_waitgroup_deinit(pool[i]);
    }
  }

}

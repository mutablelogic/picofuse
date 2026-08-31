#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_t tag(uintptr_t n) { return (sys_event_t)n; }

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(3);
  test_assert(queue != NULL);

  test_assert(sys_event_queue_try_push(queue, tag(1)));
  test_assert(sys_event_queue_try_push(queue, tag(2)));
  test_assert(sys_event_queue_try_push(queue, tag(3)));
  test_assert(sys_event_queue_size(queue) == 3);

  // try_push() never overwrites - it fails outright once full.
  test_assert(!sys_event_queue_try_push(queue, tag(4)));
  test_assert(sys_event_queue_size(queue) == 3);

  // push(), by contrast, always succeeds by overwriting the oldest entry.
  test_assert(sys_event_queue_push(queue, tag(4)));
  test_assert(sys_event_queue_size(queue) == 3);

  // Entry 1 (the oldest) was dropped; 2, 3, 4 remain in order.
  test_assert(sys_event_queue_try_pop(queue) == tag(2));
  test_assert(sys_event_queue_try_pop(queue) == tag(3));
  test_assert(sys_event_queue_try_pop(queue) == tag(4));
  test_assert(sys_event_queue_empty(queue));

  // Overwriting repeatedly on a full queue keeps working (exercises the
  // head==tail wraparound path more than once).
  test_assert(sys_event_queue_try_push(queue, tag(10)));
  test_assert(sys_event_queue_try_push(queue, tag(11)));
  test_assert(sys_event_queue_try_push(queue, tag(12)));
  for (uintptr_t i = 13; i <= 20; i++) {
    test_assert(sys_event_queue_push(queue, tag(i)));
    test_assert(sys_event_queue_size(queue) == 3);
  }
  test_assert(sys_event_queue_try_pop(queue) == tag(18));
  test_assert(sys_event_queue_try_pop(queue) == tag(19));
  test_assert(sys_event_queue_try_pop(queue) == tag(20));
  test_assert(sys_event_queue_empty(queue));

  // push() on a full but not-yet-shut-down queue still overwrites even when
  // the dropped entry was never popped by anyone - it's just gone.
  test_assert(sys_event_queue_try_push(queue, tag(30)));
  test_assert(sys_event_queue_try_push(queue, tag(31)));
  test_assert(sys_event_queue_try_push(queue, tag(32)));
  test_assert(sys_event_queue_push(queue, tag(33)));
  test_assert(sys_event_queue_size(queue) == 3);

  sys_event_queue_deinit(queue);

}

#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_t tag(uintptr_t n) { return (sys_event_t)n; }

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);

  // NULL events are rejected by both push variants.
  test_assert(!sys_event_queue_push(queue, NULL));
  test_assert(!sys_event_queue_try_push(queue, NULL));
  test_assert(sys_event_queue_empty(queue));

  // FIFO ordering is preserved across a full push/pop cycle.
  for (uintptr_t i = 1; i <= 4; i++) {
    test_assert(sys_event_queue_try_push(queue, tag(i)));
  }
  test_assert(sys_event_queue_size(queue) == 4);
  test_assert(!sys_event_queue_empty(queue));

  for (uintptr_t i = 1; i <= 4; i++) {
    test_assert(sys_event_queue_try_pop(queue) == tag(i));
  }
  test_assert(sys_event_queue_size(queue) == 0);
  test_assert(sys_event_queue_empty(queue));

  // try_pop() on an empty queue returns NULL and leaves the queue untouched
  // (regression test: this used to underflow the internal count).
  test_assert(sys_event_queue_try_pop(queue) == NULL);
  test_assert(sys_event_queue_size(queue) == 0);
  test_assert(sys_event_queue_try_pop(queue) == NULL);
  test_assert(sys_event_queue_size(queue) == 0);

  // Wrapping around the ring buffer preserves order too.
  test_assert(sys_event_queue_try_push(queue, tag(10)));
  test_assert(sys_event_queue_try_push(queue, tag(11)));
  test_assert(sys_event_queue_try_pop(queue) == tag(10));
  test_assert(sys_event_queue_try_push(queue, tag(12)));
  test_assert(sys_event_queue_try_push(queue, tag(13)));
  test_assert(sys_event_queue_try_pop(queue) == tag(11));
  test_assert(sys_event_queue_try_pop(queue) == tag(12));
  test_assert(sys_event_queue_try_pop(queue) == tag(13));
  test_assert(sys_event_queue_empty(queue));

  // pop() returns immediately, without blocking, when an item is already
  // queued.
  test_assert(sys_event_queue_push(queue, tag(99)));
  test_assert(sys_event_queue_pop(queue) == tag(99));

  sys_event_queue_deinit(queue);

}

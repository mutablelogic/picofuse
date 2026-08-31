#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_t tag(uintptr_t n) { return (sys_event_t)n; }

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(2);
  test_assert(queue != NULL);

  // peek() on an empty queue returns NULL without removing anything.
  test_assert(sys_event_queue_lock(queue));
  test_assert(sys_event_queue_peek(queue) == NULL);
  test_assert(sys_event_queue_unlock(queue));
  test_assert(sys_event_queue_empty(queue));

  test_assert(sys_event_queue_try_push(queue, tag(7)));

  // peek() repeatedly returns the same (oldest) event without consuming it.
  test_assert(sys_event_queue_lock(queue));
  test_assert(sys_event_queue_peek(queue) == tag(7));
  test_assert(sys_event_queue_peek(queue) == tag(7));
  test_assert(sys_event_queue_unlock(queue));
  test_assert(sys_event_queue_size(queue) == 1);

  // Pushing a second event doesn't change what peek() reports - it's still
  // the oldest (tail) entry.
  test_assert(sys_event_queue_try_push(queue, tag(8)));
  test_assert(sys_event_queue_lock(queue));
  test_assert(sys_event_queue_peek(queue) == tag(7));
  test_assert(sys_event_queue_unlock(queue));
  test_assert(sys_event_queue_size(queue) == 2);
  test_assert(!sys_event_queue_empty(queue));

  // Popping advances what peek() sees.
  test_assert(sys_event_queue_try_pop(queue) == tag(7));
  test_assert(sys_event_queue_lock(queue));
  test_assert(sys_event_queue_peek(queue) == tag(8));
  test_assert(sys_event_queue_unlock(queue));

  test_assert(sys_event_queue_try_pop(queue) == tag(8));
  test_assert(sys_event_queue_empty(queue));

  // lock()/unlock() pair cleanly, repeatedly, on the same queue.
  test_assert(sys_event_queue_lock(queue));
  test_assert(sys_event_queue_unlock(queue));
  test_assert(sys_event_queue_lock(queue));
  test_assert(sys_event_queue_unlock(queue));

  sys_event_queue_deinit(queue);

}

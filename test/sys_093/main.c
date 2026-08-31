#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

test_main_sys(0) {

  // capacity == 0 is rejected outright.
  test_assert(sys_event_queue_init(0) == NULL);

  // A capacity large enough to overflow the internal size computation is
  // rejected rather than silently wrapping into an undersized allocation.
  test_assert(sys_event_queue_init(SIZE_MAX) == NULL);

  // A normal capacity succeeds and starts out empty.
  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);
  test_assert(sys_event_queue_size(queue) == 0);
  test_assert(sys_event_queue_empty(queue));

  sys_event_queue_deinit(queue);

  // Deinitializing NULL is a safe no-op.
  sys_event_queue_deinit(NULL);

  // A single-slot queue is a valid (if degenerate) capacity.
  sys_event_queue_t *tiny = sys_event_queue_init(1);
  test_assert(tiny != NULL);
  test_assert(sys_event_queue_try_push(tiny, (sys_event_t)(uintptr_t)1));
  test_assert(!sys_event_queue_try_push(tiny, (sys_event_t)(uintptr_t)2));
  sys_event_queue_deinit(tiny);

}

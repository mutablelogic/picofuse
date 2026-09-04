#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

test_main_sys(0) {

  // Every public function must treat a NULL queue as invalid and report a
  // safe value - never crash, and never block.
  sys_event_queue_t *null_queue = NULL;

  test_assert(!sys_event_queue_push(null_queue, (sys_event_t)(uintptr_t)1));
  test_assert(!sys_event_queue_try_push(null_queue, (sys_event_t)(uintptr_t)1));
  test_assert(sys_event_queue_peek(null_queue) == NULL);
  test_assert(sys_event_queue_pop(null_queue) == NULL);
  test_assert(sys_event_queue_try_pop(null_queue) == NULL);
  test_assert(sys_event_queue_timed_pop(null_queue, 10) == NULL);
  test_assert(sys_event_queue_size(null_queue) == 0);
  test_assert(sys_event_queue_empty(null_queue));
  test_assert(!sys_event_queue_lock(null_queue));
  test_assert(!sys_event_queue_unlock(null_queue));
  sys_event_queue_shutdown(null_queue); // must not crash
  sys_event_queue_deinit(null_queue);   // must not crash

  // A live queue also rejects NULL payloads on both push variants (not a
  // NULL-queue case, but the other half of "NULL is never a valid event").
  sys_event_queue_t *queue = sys_event_queue_init(2);
  test_assert(queue != NULL);
  test_assert(!sys_event_queue_push(queue, NULL));
  test_assert(!sys_event_queue_try_push(queue, NULL));
  test_assert(sys_event_queue_empty(queue));
  sys_event_queue_deinit(queue);
}

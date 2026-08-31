#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_queue_t *_other_queue;
static sys_atomic_t _reentrant_result_ok;

static void inert_event(sys_event_t event) { (void)event; }

static void on_event_check_reentrancy(sys_event_t event) {
  (void)event;
  // Calling sys_runloop_run() again while already running must fail
  // immediately (0) rather than recursing into a second loop or clobbering
  // the singleton mid-use.
  uint32_t result =
      sys_runloop_run(1, _other_queue, NULL, inert_event, NULL, NULL);
  sys_atomic_set(&_reentrant_result_ok, result == 0 ? 1 : 0);
  sys_runloop_shutdown(0);
}

static void on_event_shutdown(sys_event_t event) {
  (void)event;
  sys_runloop_shutdown(0);
}

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);
  _other_queue = sys_event_queue_init(4);
  test_assert(_other_queue != NULL);

  sys_atomic_init(&_reentrant_result_ok, 0);
  test_assert(sys_event_queue_try_push(queue, (sys_event_t)(uintptr_t)1));
  sys_runloop_run(1, queue, NULL, on_event_check_reentrancy, NULL, NULL);
  test_assert(sys_atomic_get(&_reentrant_result_ok) == 1);

  // The nested call must not have left the singleton in a broken state -
  // _other_queue was never actually started, so a legitimate run using it
  // now must work normally.
  test_assert(sys_event_queue_try_push(_other_queue, (sys_event_t)(uintptr_t)2));
  uint32_t result =
      sys_runloop_run(1, _other_queue, NULL, on_event_shutdown, NULL, NULL);
  test_assert(result == 0);

  // A fresh run on yet another queue also still works fine - the singleton
  // was properly reset after each prior run. (A queue that has already been
  // shut down by a prior run can never be reused - sys_event_queue_shutdown()
  // is permanent - so this needs a new queue, not `queue` again.)
  sys_event_queue_t *third_queue = sys_event_queue_init(4);
  test_assert(third_queue != NULL);
  test_assert(sys_event_queue_try_push(third_queue, (sys_event_t)(uintptr_t)3));
  result = sys_runloop_run(1, third_queue, NULL, on_event_shutdown, NULL, NULL);
  test_assert(result == 0);

  sys_event_queue_deinit(queue);
  sys_event_queue_deinit(_other_queue);
  sys_event_queue_deinit(third_queue);

}

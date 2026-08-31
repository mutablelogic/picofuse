#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static uint32_t _received;

static void on_event(sys_event_t event) {
  _received = (uint32_t)(uintptr_t)event;
  sys_runloop_shutdown(123);
}

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);

  // NULL queue / NULL event_fn are rejected outright, returning 0 rather
  // than crashing or starting a loop with no way to dispatch events.
  test_assert(sys_runloop_run(1, NULL, NULL, on_event, NULL, NULL) == 0);
  test_assert(sys_runloop_run(1, queue, NULL, NULL, NULL, NULL) == 0);

  // post() before any run has ever started fails - there's no active
  // singleton to post to yet.
  test_assert(!sys_runloop_post((sys_event_t)(uintptr_t)0xFF));

  // Basic round trip: an event already queued gets delivered, the handler
  // requests shutdown with a specific exit value, and that value comes back
  // out of sys_runloop_run().
  test_assert(sys_event_queue_try_push(queue, (sys_event_t)(uintptr_t)0xAB));
  uint32_t result = sys_runloop_run(1, queue, NULL, on_event, NULL, NULL);
  test_assert(result == 123);
  test_assert(_received == 0xAB);

  // Once sys_runloop_run() has returned, the loop is no longer running -
  // shutdown() is a safe no-op and post() fails again.
  sys_runloop_shutdown(999);
  test_assert(!sys_runloop_post((sys_event_t)(uintptr_t)1));

  sys_event_queue_deinit(queue);

}

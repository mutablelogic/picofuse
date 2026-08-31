#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_queue_t *_queue;
static sys_atomic_t _processed;
static sys_waitgroup_t *_wg;

static void on_event(sys_event_t event) {
  (void)event;
  sys_atomic_inc(&_processed);
}

static void delayed_shutdown(void *arg) {
  (void)arg;
  // Fires quickly, likely before all pre-seeded events have been drained -
  // exercising "shut down while work is still pending" rather than "shut
  // down once already idle".
  sys_sleep_ms(10);
  sys_runloop_shutdown(55);
  test_assert(sys_waitgroup_done(_wg));
}

static void dispatch(sys_thread_func_t func, void *arg) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, arg, 1));
#else
  test_assert(sys_thread_create(func, arg));
#endif
}

test_main_sys(0) {

  // shutdown() when no loop is running at all is a safe no-op.
  sys_runloop_shutdown(1);

  // Case 1: shutdown() wakes an otherwise-idle loop with nothing queued.
  _queue = sys_event_queue_init(4);
  test_assert(_queue != NULL);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(delayed_shutdown, NULL);

  uint32_t result = sys_runloop_run(1, _queue, NULL, on_event, NULL, NULL);
  test_assert(result == 55);

  // Wait for delayed_shutdown() to fully finish (including its own tail
  // end inside sys_event_queue_shutdown()) before tearing the queue down -
  // sys_runloop_run() returning only means worker 0 is done, not that every
  // other thread that touched the queue has too.
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  sys_event_queue_deinit(_queue);

  // Case 2: shutdown() while events are still pending drains all of them
  // before the loop actually stops - it doesn't just abandon the queue.
  _queue = sys_event_queue_init(4);
  test_assert(_queue != NULL);
  sys_atomic_init(&_processed, 0);
  test_assert(sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)1));
  test_assert(sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)2));
  test_assert(sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)3));

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(delayed_shutdown, NULL);

  result = sys_runloop_run(1, _queue, NULL, on_event, NULL, NULL);
  test_assert(result == 55);
  test_assert(sys_atomic_get(&_processed) == 3);

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  sys_event_queue_deinit(_queue);

}

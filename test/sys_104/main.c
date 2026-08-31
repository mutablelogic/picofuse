#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_queue_t *_queue;
static uint32_t _order[2];
static size_t _order_count;
static sys_waitgroup_t *_wg;

static void poster(void *arg) {
  (void)arg;
  sys_sleep_ms(20);
  test_assert(sys_runloop_post((sys_event_t)(uintptr_t)2));
  test_assert(sys_waitgroup_done(_wg));
}

static void dispatch(sys_thread_func_t func, void *arg) {
#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(func, arg, 1));
#else
  test_assert(sys_thread_create(func, arg));
#endif
}

static void on_event(sys_event_t event) {
  _order[_order_count++] = (uint32_t)(uintptr_t)event;
  if (_order_count == 2) {
    sys_runloop_shutdown(0);
    // Shutdown just took effect (we're still inside the callback that
    // triggered it) - post() must reflect that immediately, not only once
    // sys_runloop_run() has actually returned.
    test_assert(!sys_runloop_post((sys_event_t)(uintptr_t)999));
  }
}

test_main_sys(0) {

  // post() before the loop is running fails outright - nothing to post to
  // yet.
  test_assert(!sys_runloop_post((sys_event_t)(uintptr_t)1));

  _queue = sys_event_queue_init(4);
  test_assert(_queue != NULL);
  _order_count = 0;

  // One event pre-seeded directly on the queue (before the loop even
  // starts), one posted via sys_runloop_post() from another thread/core
  // once the loop is confirmed running - both must be delivered, in order.
  test_assert(sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)1));

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(poster, NULL);

  uint32_t result = sys_runloop_run(1, _queue, NULL, on_event, NULL, NULL);
  test_assert(result == 0);
  test_assert(_order_count == 2);
  test_assert(_order[0] == 1 && _order[1] == 2);

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  // The loop has returned - post() fails again now that nothing is running.
  test_assert(!sys_runloop_post((sys_event_t)(uintptr_t)3));

  sys_event_queue_deinit(_queue);

}

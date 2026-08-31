#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_atomic_t _poll_count;
static sys_waitgroup_t *_wg;

static void on_poll(void) { sys_atomic_inc(&_poll_count); }

static void unreachable_event(sys_event_t event) {
  (void)event;
  test_assert(false); // nothing is ever pushed in this test
}

static void watcher(void *arg) {
  (void)arg;
  while (sys_atomic_get(&_poll_count) < 3) {
    sys_sleep_ms(10);
  }
  sys_runloop_shutdown(0);
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

  // poll_fn must keep firing on its own while the queue is completely
  // idle - it isn't tied to events arriving at all.
  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);
  sys_atomic_init(&_poll_count, 0);

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(watcher, NULL);

  uint32_t result =
      sys_runloop_run(1, queue, NULL, unreachable_event, on_poll, NULL);
  test_assert(result == 0);
  test_assert(sys_atomic_get(&_poll_count) >= 3);

  // Wait for watcher() to fully finish (including its own tail end inside
  // sys_event_queue_shutdown()) before tearing the queue down.
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  sys_event_queue_deinit(queue);

}

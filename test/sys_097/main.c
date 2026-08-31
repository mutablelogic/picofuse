#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_queue_t *_queue;
static sys_waitgroup_t *_wg;

static void delayed_pusher(void *arg) {
  (void)arg;
  sys_sleep_ms(50);
  test_assert(sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)42));
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

  _queue = sys_event_queue_init(4);
  test_assert(_queue != NULL);

  // timeout_ms == 0 delegates straight to sys_event_queue_pop() - confirm
  // that alias actually blocks rather than returning immediately by racing
  // it against a delayed push.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(delayed_pusher, NULL);
  test_assert(sys_event_queue_timed_pop(_queue, 0) == (sys_event_t)(uintptr_t)42);
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);
  test_assert(sys_event_queue_empty(_queue));

  // No event ever arrives: timed_pop() must return NULL once its timeout
  // elapses, not hang indefinitely.
  uint64_t before = sys_timestamp_ms();
  test_assert(sys_event_queue_timed_pop(_queue, 100) == NULL);
  uint64_t elapsed = sys_timestamp_ms() - before;
  test_assert(elapsed >= 100);
  // Generous upper bound: only the internal poll interval should ever be
  // added on top of the requested timeout.
  test_assert(elapsed < 1000);

  // An event pushed from another thread/core part-way through the wait
  // wakes timed_pop() well before its (generous) deadline.
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(delayed_pusher, NULL);

  before = sys_timestamp_ms();
  sys_event_t event = sys_event_queue_timed_pop(_queue, 5000);
  elapsed = sys_timestamp_ms() - before;
  test_assert(event == (sys_event_t)(uintptr_t)42);
  test_assert(elapsed < 5000);

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  sys_event_queue_deinit(_queue);

}

#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>
#include <test/test.h>

static sys_event_queue_t *_queue;
static sys_waitgroup_t *_wg;
static sys_event_t _popped;
static volatile bool _pop_returned;

static void blocked_popper(void *arg) {
  (void)arg;
  _popped = sys_event_queue_pop(_queue);
  _pop_returned = true;
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

  // A consumer blocked in pop() on an empty queue is woken by shutdown() and
  // returns NULL, rather than hanging forever.
  _pop_returned = false;
  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  dispatch(blocked_popper, NULL);

  sys_sleep_ms(100); // give the popper time to actually start blocking
  test_assert(!_pop_returned);

  sys_event_queue_shutdown(_queue);
  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  test_assert(_pop_returned);
  test_assert(_popped == NULL);

  // Pushing after shutdown always fails, by either variant.
  test_assert(!sys_event_queue_push(_queue, (sys_event_t)(uintptr_t)1));
  test_assert(!sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)1));

  // Shutdown is idempotent.
  sys_event_queue_shutdown(_queue);

  sys_event_queue_deinit(_queue);

  // A queue shut down while events are still pending drains them first -
  // pop() only starts returning NULL once it is both shut down and empty.
  sys_event_queue_t *draining = sys_event_queue_init(2);
  test_assert(draining != NULL);
  test_assert(sys_event_queue_try_push(draining, (sys_event_t)(uintptr_t)7));
  test_assert(sys_event_queue_try_push(draining, (sys_event_t)(uintptr_t)8));
  sys_event_queue_shutdown(draining);
  test_assert(sys_event_queue_pop(draining) == (sys_event_t)(uintptr_t)7);
  test_assert(sys_event_queue_pop(draining) == (sys_event_t)(uintptr_t)8);
  test_assert(sys_event_queue_pop(draining) == NULL);
  test_assert(sys_event_queue_pop(draining) == NULL);

  sys_event_queue_deinit(draining);

}

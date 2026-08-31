#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

#define NUM_WORKERS 2
#define NUM_EVENTS 20

static sys_atomic_t _init_calls[NUM_WORKERS];
static sys_atomic_t _exit_calls[NUM_WORKERS];
static sys_atomic_t _events_seen;

static void on_init(uint8_t worker_index) {
  test_assert(worker_index < NUM_WORKERS);
  sys_atomic_inc(&_init_calls[worker_index]);
}

static void on_exit(uint8_t worker_index) {
  test_assert(worker_index < NUM_WORKERS);
  sys_atomic_inc(&_exit_calls[worker_index]);
}

static void on_event(sys_event_t event) {
  (void)event;
  if (sys_atomic_inc(&_events_seen) >= NUM_EVENTS) {
    sys_runloop_shutdown(0);
  }
}

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(NUM_EVENTS);
  test_assert(queue != NULL);

  for (int i = 0; i < NUM_WORKERS; i++) {
    sys_atomic_init(&_init_calls[i], 0);
    sys_atomic_init(&_exit_calls[i], 0);
  }
  sys_atomic_init(&_events_seen, 0);

  for (int i = 0; i < NUM_EVENTS; i++) {
    test_assert(sys_event_queue_try_push(queue, (sys_event_t)(uintptr_t)(i + 1)));
  }

  uint32_t result =
      sys_runloop_run(NUM_WORKERS, queue, on_init, on_event, NULL, on_exit);
  test_assert(result == 0);

  // Every event pushed was consumed exactly once, however the two workers
  // happened to split the work between them.
  test_assert(sys_atomic_get(&_events_seen) == NUM_EVENTS);

  // sys_runloop_run() only returns once every worker it actually started
  // has been through both its init and exit hook exactly once (that's what
  // the internal waitgroup guarantees) - true regardless of whether that
  // worker ever got handed an event to process.
  test_assert(sys_atomic_get(&_init_calls[0]) == 1);
  test_assert(sys_atomic_get(&_exit_calls[0]) == 1);
  if (sys_thread_numcores() > 1) {
    test_assert(sys_atomic_get(&_init_calls[1]) == 1);
    test_assert(sys_atomic_get(&_exit_calls[1]) == 1);
  }

  sys_event_queue_deinit(queue);

}

#include <picofuse/sys.h>
#include <stddef.h>
#include <stdint.h>
#include <test/test.h>

// Pico only has one spare core (core1); host platforms can spawn several
// producers via sys_thread_create.
#if defined(SYSTEM_NAME_PICO)
#define NUM_PRODUCERS 1
#else
#define NUM_PRODUCERS 4
#endif

#define ITEMS_PER_PRODUCER 50
#define TOTAL_ITEMS (NUM_PRODUCERS * ITEMS_PER_PRODUCER)

static sys_waitgroup_t *_producers_done;
static sys_atomic_t _seen[TOTAL_ITEMS];
static sys_atomic_t _consumed;
static sys_atomic_t _failures;

static void on_event(sys_event_t event) {
  uintptr_t global_id = (uintptr_t)event - 1;
  if (global_id >= TOTAL_ITEMS) {
    sys_atomic_inc(&_failures);
    return;
  }
  if (sys_atomic_inc(&_seen[global_id]) != 1) {
    // Same event delivered twice, or two producers collided on the same
    // global id - either way the pipeline handed out corrupted data.
    sys_atomic_inc(&_failures);
    return;
  }
  if (sys_atomic_inc(&_consumed) == TOTAL_ITEMS) {
    sys_runloop_shutdown(0);
  }
}

static void producer(void *arg) {
  uintptr_t producer_id = (uintptr_t)arg;
  for (uintptr_t i = 0; i < ITEMS_PER_PRODUCER; i++) {
    uintptr_t global_id = producer_id * ITEMS_PER_PRODUCER + i;
    sys_event_t event = (sys_event_t)(global_id + 1); // never NULL

    // Retries under contention: the queue is deliberately much smaller than
    // TOTAL_ITEMS, so producers routinely find it full and must wait for
    // the runloop to drain it via on_event().
    while (!sys_runloop_post(event)) {
      sys_sleep_ms(0);
    }
  }
  test_assert(sys_waitgroup_done(_producers_done));
}

test_main_sys(0) {

  sys_event_queue_t *queue = sys_event_queue_init(8);
  test_assert(queue != NULL);

  for (size_t i = 0; i < TOTAL_ITEMS; i++) {
    sys_atomic_init(&_seen[i], 0);
  }
  sys_atomic_init(&_consumed, 0);
  sys_atomic_init(&_failures, 0);

  _producers_done = sys_waitgroup_init();
  test_assert(_producers_done != NULL);
  test_assert(sys_waitgroup_add(_producers_done, NUM_PRODUCERS));

  for (uintptr_t i = 0; i < NUM_PRODUCERS; i++) {
#if defined(SYSTEM_NAME_PICO)
    // The runloop below is single-worker, so core 1 is free for the
    // producer; worker 0 (the calling thread) runs the loop itself.
    test_assert(sys_thread_create_on_core(producer, (void *)i, 1));
#else
    test_assert(sys_thread_create(producer, (void *)i));
#endif
  }

  uint32_t result = sys_runloop_run(1, queue, NULL, on_event, NULL, NULL);
  test_assert(result == 0);

  sys_waitgroup_wait(_producers_done);
  sys_waitgroup_deinit(_producers_done);

  test_assert(sys_atomic_get(&_failures) == 0);
  test_assert(sys_atomic_get(&_consumed) == TOTAL_ITEMS);

  sys_event_queue_deinit(queue);

}

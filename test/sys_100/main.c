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

#define ITEMS_PER_PRODUCER 100
#define TOTAL_ITEMS (NUM_PRODUCERS * ITEMS_PER_PRODUCER)

static sys_event_queue_t *_queue;
static sys_waitgroup_t *_producers_done;
static sys_atomic_t _seen[TOTAL_ITEMS];
static sys_atomic_t _consumed;
static sys_atomic_t _failures;

static void producer(void *arg) {
  uintptr_t producer_id = (uintptr_t)arg;
  for (uintptr_t i = 0; i < ITEMS_PER_PRODUCER; i++) {
    uintptr_t global_id = producer_id * ITEMS_PER_PRODUCER + i;
    sys_event_t event = (sys_event_t)(global_id + 1); // never NULL

    // Retries under try_push() rather than push(): the queue is
    // deliberately much smaller than TOTAL_ITEMS, so producers routinely
    // find it full and must wait for the consumer, rather than silently
    // overwriting entries push() would drop.
    while (!sys_event_queue_try_push(_queue, event)) {
      sys_sleep_ms(0);
    }
  }
  test_assert(sys_waitgroup_done(_producers_done));
}

// Pops and validates a single event. A real duplicate-or-corrupted delivery
// (the same global id handed out twice, or a payload outside the valid
// range) is only detectable this way, since the queue only ever hands each
// pointer to one consumer if it's working correctly.
static void consume_one(void) {
  sys_event_t event = sys_event_queue_pop(_queue);
  if (event == NULL) {
    return;
  }

  uintptr_t global_id = (uintptr_t)event - 1;
  if (global_id >= TOTAL_ITEMS) {
    sys_atomic_inc(&_failures);
    return;
  }
  if (sys_atomic_inc(&_seen[global_id]) != 1) {
    sys_atomic_inc(&_failures);
    return;
  }
  sys_atomic_inc(&_consumed);
}

test_main_sys(0) {

  // Deliberately small relative to TOTAL_ITEMS, so producers routinely find
  // it full (try_push()'s fail path) and the consumer routinely finds it
  // empty (pop()'s blocking/wakeup path) under real contention - not just
  // "many threads touching one queue".
  _queue = sys_event_queue_init(8);
  test_assert(_queue != NULL);

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
    test_assert(sys_thread_create_on_core(producer, (void *)i, 1));
#else
    test_assert(sys_thread_create(producer, (void *)i));
#endif
  }

  // The main thread/core is the sole consumer, popping (blocking whenever
  // the queue runs dry) until every produced item has been accounted for.
  while (sys_atomic_get(&_consumed) < TOTAL_ITEMS) {
    consume_one();
  }

  sys_waitgroup_wait(_producers_done);
  sys_waitgroup_deinit(_producers_done);

  test_assert(sys_atomic_get(&_failures) == 0);
  test_assert(sys_atomic_get(&_consumed) == TOTAL_ITEMS);
  test_assert(sys_event_queue_empty(_queue));

  sys_event_queue_deinit(_queue);

}

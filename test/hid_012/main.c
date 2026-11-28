#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

#define TIMERS 8
#define ROUNDS 20000
#define CONSUMERS 4

// Timers fire at the same time - on a host each on a thread of its own - and take
// event slots from the one pool, while consumers free them. A slot taken twice is
// queued twice, so it's popped again after it's freed.

static hid_t *instance;
static sys_event_queue_t *queue;
static sys_atomic_t popped;
static sys_atomic_t bad;
static sys_waitgroup_t *wg;

// A consumer can wait for longer than the timeout while others take every event, as
// the queue isn't fair, so it's only a stall if nothing was taken in that time
static void consume(void) {
  while (sys_atomic_get(&popped) < ROUNDS) {
    uint32_t before = sys_atomic_get(&popped);
    sys_event_t event = sys_event_queue_timed_pop(queue, 500);
    if (event == NULL) {
      if (sys_atomic_get(&popped) != before) {
        continue;
      }
      sys_atomic_inc(&bad);
      break;
    }
    hid_event_t *hid_event = (hid_event_t *)event;
    uintptr_t userdata = (uintptr_t)hid_event->data.timer.userdata;
    if (!hid_event_owned(instance, event) || hid_event->type != hid_event_type_timer ||
        userdata < 1 || userdata > TIMERS) {
      sys_atomic_inc(&bad);
      break;
    }
    hid_event_free(hid_event);
    sys_atomic_inc(&popped);
  }
}

static void consumer(void *arg) {
  (void)arg;
  consume();
  sys_waitgroup_done(wg);
}

test_main_sys(0) {
  queue = sys_event_queue_init(64);
  test_assert(queue != NULL);
  instance = hid_init(queue);
  test_assert(instance != NULL);
  sys_atomic_init(&popped, 0);
  sys_atomic_init(&bad, 0);
  wg = sys_waitgroup_init();
  test_assert(wg != NULL);

  hid_device_t *devices[TIMERS];
  for (uintptr_t i = 0; i < TIMERS; i++) {
    devices[i] = hid_register_timer(instance, (uint32_t)i, 1, true, (void *)(i + 1));
    test_assert(devices[i] != NULL);
  }

  // Consumers on other threads, where there are other cores, and this one
  uint8_t cores = sys_thread_numcores();
  int others = cores > CONSUMERS ? CONSUMERS - 1 : cores - 1;
  for (int i = 0; i < others; i++) {
    test_assert(sys_waitgroup_add(wg, 1));
    test_assert(sys_thread_create(consumer, NULL));
  }
  consume();
  sys_waitgroup_wait(wg);
  test_assert(sys_atomic_get(&bad) == 0);
  test_assert(sys_atomic_get(&popped) >= ROUNDS);

  for (size_t i = 0; i < TIMERS; i++) {
    test_assert(hid_deregister(instance, devices[i]));
  }
  sys_event_t event;
  while ((event = sys_event_queue_try_pop(queue)) != NULL) {
    test_assert(hid_event_owned(instance, event));
    hid_event_free(event);
  }
  sys_waitgroup_deinit(wg);
  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}

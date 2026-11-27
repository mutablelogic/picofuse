#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

// clang warns that atomics aren't lock-free on an RP2040
#if defined(__clang__)
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

// sys_runloop_post() from a timer callback, which on Pico is an interrupt on the core
// which started the timer: worker 0's, which runs the loop. The timer keeps posting as
// the loop shuts down and after it stops.

#define EVENTS 50

static sys_timer_t *_timer;
static sys_atomic_t _accepted; // posts which returned true
static sys_atomic_t _refused;  // posts which returned false
static sys_atomic_t _received; // events dispatched
static sys_atomic_t _stopped;  // set once sys_runloop_run() has returned
static sys_atomic_t _late;     // posts accepted after that, which mustn't happen

static void on_timer(sys_timer_t *timer) {
  (void)timer;
  if (sys_runloop_post((sys_event_t)(uintptr_t)1)) {
    sys_atomic_inc(&_accepted);
    if (sys_atomic_get(&_stopped) != 0) {
      sys_atomic_inc(&_late);
    }
  } else {
    sys_atomic_inc(&_refused);
  }
}

static void on_init(uint8_t worker) {
  if (worker == 0) {
    _timer = sys_timer_init(1, on_timer, NULL);
    test_assert(_timer != NULL && sys_timer_start(_timer));
  }
}

static void on_event(sys_event_t event) {
  test_assert(event == (sys_event_t)(uintptr_t)1);
  if (sys_atomic_inc(&_received) == EVENTS) {
    sys_runloop_shutdown(9);
  }
}

static void run(uint8_t workers) {
  sys_atomic_init(&_accepted, 0);
  sys_atomic_init(&_refused, 0);
  sys_atomic_init(&_received, 0);
  sys_atomic_init(&_stopped, 0);
  sys_atomic_init(&_late, 0);

  sys_event_queue_t *queue = sys_event_queue_init(8);
  test_assert(queue != NULL);
  test_assert(sys_runloop_run(workers, queue, on_init, on_event, NULL, NULL) == 9);
  sys_atomic_set(&_stopped, 1);

  // The timer is still posting: those posts are refused
  uint32_t refused = sys_atomic_get(&_refused);
  for (int i = 0; i < 100 && sys_atomic_get(&_refused) < refused + 5; i++) {
    sys_sleep_ms(1);
  }
  sys_timer_deinit(_timer);
  sys_event_queue_deinit(queue);

  // Every post which was accepted was dispatched, and none after it stopped
  test_assert(sys_atomic_get(&_received) >= EVENTS);
  test_assert(sys_atomic_get(&_accepted) == sys_atomic_get(&_received));
  test_assert(sys_atomic_get(&_refused) >= refused + 5);
  test_assert(sys_atomic_get(&_late) == 0);
}

test_main_sys(0) {
  run(1);
  run(0);
}

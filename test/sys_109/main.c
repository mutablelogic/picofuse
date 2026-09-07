#include <picofuse/sys.h>
#include <test/test.h>

#include "../../src/picofuse/sys/pico/flash_pause.h"

static sys_atomic_t _worker_ready;
static sys_atomic_t _pause_complete;

static void on_init(uint8_t worker_index) {
  if (worker_index == 1u) {
    sys_atomic_set(&_worker_ready, 1);
  }
}

static void on_event(sys_event_t event) { (void)event; }

static void on_poll(void) {
  if (sys_atomic_get(&_worker_ready) == 0u ||
      sys_atomic_get(&_pause_complete) != 0u) {
    return;
  }

  test_assert(_sys_pico_flash_pause_request(100));
  _sys_pico_flash_pause_release();
  sys_atomic_set(&_pause_complete, 1);
  sys_runloop_shutdown(0);
}

test_main_sys(0) {
  sys_atomic_init(&_worker_ready, 0);
  sys_atomic_init(&_pause_complete, 0);

  sys_event_queue_t *queue = sys_event_queue_init(1);
  test_assert(queue != NULL);
  test_assert(sys_runloop_run(2, queue, on_init, on_event, on_poll, NULL) == 0);
  test_assert(sys_atomic_get(&_worker_ready) != 0u);
  test_assert(sys_atomic_get(&_pause_complete) != 0u);
  sys_event_queue_deinit(queue);
}
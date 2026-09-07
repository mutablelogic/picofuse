#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

#define FLASH_SECTOR_BYTES 4096u
#define FLASH_STRESS_ROUNDS 16u

static hw_block_t *_blocks[BLOCK_MAX_CAPACITY];
static uint8_t _write_data[FLASH_SECTOR_BYTES];
static uint8_t _read_data[FLASH_SECTOR_BYTES];
static sys_atomic_t _worker_ready;
static sys_atomic_t _completed;
static sys_atomic_t _timer_ticks;
static sys_atomic_t _timer_events_posted;
static sys_atomic_t _timer_events_processed;
static sys_event_queue_t *_queue;
static size_t _round;

static void on_init(uint8_t worker_index) {
  if (worker_index == 1u) {
    sys_atomic_set(&_worker_ready, 1);
  }
}

static void on_timer(sys_timer_t *timer) {
  (void)timer;
  sys_atomic_inc(&_timer_ticks);
  if (sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)1)) {
    sys_atomic_inc(&_timer_events_posted);
  }
}

static void on_event(sys_event_t event) {
  test_assert(event == (sys_event_t)(uintptr_t)1);
  sys_atomic_inc(&_timer_events_processed);
}

static void on_poll(void) {
  if (sys_atomic_get(&_worker_ready) == 0u ||
      sys_atomic_get(&_completed) != 0u) {
    return;
  }

  if (_round < FLASH_STRESS_ROUNDS) {
    hw_block_t *block = _blocks[_round % BLOCK_MAX_CAPACITY];
    for (size_t i = 0; i < sizeof(_write_data); i++) {
      _write_data[i] = (uint8_t)(i * 37u + _round);
    }

    test_assert(hw_block_erase(block, 0));
    test_assert(hw_block_write(block, 0, _write_data));
    test_assert(hw_block_read(block, 0, _read_data));
    test_assert(memcmp(_read_data, _write_data, sizeof(_read_data)) == 0);

    _round++;
  }
  if (_round == FLASH_STRESS_ROUNDS && sys_atomic_get(&_timer_ticks) >= 10u &&
      sys_atomic_get(&_timer_events_processed) >= 10u) {
    sys_atomic_set(&_completed, 1);
    sys_runloop_shutdown(0);
  }
}

test_main_hw(0) {
  size_t capacity = hw_block_flash_get_capacity();
  test_assert(capacity >= BLOCK_MAX_CAPACITY * FLASH_SECTOR_BYTES);

  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    _blocks[i] = hw_block_flash_init(FLASH_SECTOR_BYTES);
    test_assert(_blocks[i] != NULL);
    test_assert(hw_block_count(_blocks[i]) == 1u);
    test_assert(hw_block_size(_blocks[i]) == FLASH_SECTOR_BYTES);
  }
  test_assert(hw_block_flash_init(FLASH_SECTOR_BYTES) == NULL);
  test_assert(hw_block_flash_get_capacity() ==
              capacity - BLOCK_MAX_CAPACITY * FLASH_SECTOR_BYTES);

  hw_block_deinit(_blocks[1]);
  test_assert(hw_block_flash_get_capacity() ==
              capacity - BLOCK_MAX_CAPACITY * FLASH_SECTOR_BYTES);
  _blocks[1] = hw_block_flash_init(FLASH_SECTOR_BYTES);
  test_assert(_blocks[1] != NULL);
  test_assert(hw_block_flash_get_capacity() ==
              capacity - BLOCK_MAX_CAPACITY * FLASH_SECTOR_BYTES);

  sys_atomic_init(&_worker_ready, 0);
  sys_atomic_init(&_completed, 0);
  sys_atomic_init(&_timer_ticks, 0);
  sys_atomic_init(&_timer_events_posted, 0);
  sys_atomic_init(&_timer_events_processed, 0);
  _round = 0;

  _queue = sys_event_queue_init(32);
  test_assert(_queue != NULL);
  sys_timer_t *timer = sys_timer_init(10, on_timer, NULL);
  test_assert(timer != NULL);
  test_assert(sys_timer_start(timer));
  test_assert(sys_runloop_run(2, _queue, on_init, on_event, on_poll, NULL) ==
              0);
  test_assert(sys_atomic_get(&_worker_ready) != 0u);
  test_assert(sys_atomic_get(&_completed) != 0u);
  sys_timer_deinit(timer);
  test_assert(sys_atomic_get(&_timer_ticks) >= 10u);
  test_assert(sys_atomic_get(&_timer_events_posted) >= 10u);
  test_assert(sys_atomic_get(&_timer_events_processed) ==
              sys_atomic_get(&_timer_events_posted));
  sys_event_queue_deinit(_queue);
  _queue = NULL;

  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    hw_block_deinit(_blocks[i]);
  }
  test_assert(hw_block_flash_get_capacity() == capacity);
}
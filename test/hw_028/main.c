#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#define FLASH_SECTOR_BYTES 4096u
#define FLASH_EVENT_TEST_MAX_ROUNDS 60u

static hw_block_t *_block;
// One buffer pair per core rather than a lock serializing whole rounds
// against each other: erase/write from core 1 are true no-ops today (see
// on_event()'s own doc) - they return before ever touching flash - so
// core 0's own rounds are never actually racing anyone for the block's
// real content. A cross-core lock held across the erase/write/read
// sequence would instead deadlock against _sys_pico_flash_pause_request()
// itself: core 0's erase blocks waiting for core 1 to reach its own pause
// point, which core 1 can't do while it's stuck waiting on that same
// lock - confirmed the hard way while writing this test. Once the
// core-1 -> core-0 marshaling TODO lands and core 1's writes stop being
// no-ops, this will need a real per-round handshake instead.
static uint8_t _write_data[2][FLASH_SECTOR_BYTES];
static uint8_t _read_data[2][FLASH_SECTOR_BYTES];
static sys_event_queue_t *_queue;
static sys_atomic_t _rounds;
static sys_atomic_t _core1_seen;
static sys_atomic_t _done;
static sys_atomic_t _worker_ready;

static void on_init(uint8_t worker_index) {
  if (worker_index == 1u) {
    sys_atomic_set(&_worker_ready, 1);
  }
}

static void on_timer(sys_timer_t *timer) {
  (void)timer;
  // Gated on _worker_ready (same pattern as hw_027's own on_poll()) -
  // without it, a backlog of events built up during core 1's own launch
  // delay (_sys_thread_ensure_core1_worker()'s reset-safety sleep_ms(100)
  // in sys/pico/thread.c) could let core 0 pop one and attempt a flash op
  // before core 1 has reached _sys_pico_flash_pause_worker_enter() at
  // all, races _sys_pico_flash_pause_request()'s own worker_active check
  // against core 1's startup, and fails core 0's own otherwise-valid
  // call - confirmed the hard way while writing this test.
  //
  // Short and repeating once armed - fires far more often than one
  // round's own real flash erase/program latency, so both workers keep
  // racing each other for new events rather than the queue running dry
  // between rounds.
  if (sys_atomic_get(&_worker_ready) == 0u) {
    return;
  }
  (void)sys_event_queue_try_push(_queue, (sys_event_t)(uintptr_t)1);
}

// hw_block_erase()/hw_block_write()/hw_block_read() called from inside an
// event handler - unlike hw_027's own flash exercise (issued from
// on_poll(), which sys_runloop_run() only ever calls on worker 0 - see
// sys/runloop.h's own doc on sys_runloop_poll_func_t), event_fn may run
// on *either* worker, since both pop from the same shared queue. That's
// exactly the situation the upcoming fs module will be in (see TODO.md's
// "Transparent core-1 -> core-0 flash write/erase marshaling"): erase/
// write currently fail outright when they land on core 1 - see
// _sys_pico_flash_pause_request()'s own `get_core_num() != 0` gate in
// sys/pico/flash_pause.c - while read has no such restriction.
//
// This test documents *today's* actual behavior. Once that TODO is
// implemented, the assertions in the `core == 1` branch below need to
// flip from expecting failure to expecting success.
static void on_event(sys_event_t event) {
  (void)event;

  uint32_t round = sys_atomic_get(&_rounds);
  uint8_t core = sys_thread_core();
  uint8_t *write_data = _write_data[core & 1u];
  uint8_t *read_data = _read_data[core & 1u];

  for (size_t i = 0; i < FLASH_SECTOR_BYTES; i++) {
    write_data[i] = (uint8_t)(i * 61u + round);
  }
  memset(read_data, 0, FLASH_SECTOR_BYTES);

  bool erase_ok = hw_block_erase(_block, 0);
  bool write_ok = hw_block_write(_block, 0, write_data);
  bool read_ok = hw_block_read(_block, 0, read_data);
  bool data_ok =
      read_ok && memcmp(read_data, write_data, FLASH_SECTOR_BYTES) == 0;

  // core 0: the direct path (see flash.c) - must fully succeed. Nothing
  // else can be mutating the block's real content concurrently (core 1's
  // own attempts are no-ops today - see this function's own doc), so this
  // read-after-write is reliable without any extra locking.
  //
  // core 1: today's known limitation (see TODO.md) - erase/write fail
  // outright; read is unaffected.
  bool expected = core == 0u ? (erase_ok && write_ok && read_ok && data_ok)
                             : (!erase_ok && !write_ok && read_ok);
  if (core == 1u) {
    sys_atomic_set(&_core1_seen, 1);
  }

  // Logged only when something didn't match, not every round - printf
  // traffic itself was observed to perturb the very timing this test
  // exercises (heavy RTT output from both cores competing was enough to
  // delay core 1's own pause-point acknowledgment past core 0's 1000ms
  // budget, producing a failure that had nothing to do with flash.c).
  if (!expected) {
    sys_debugf("hw_028", "round=%u core=%u erase=%d write=%d read=%d data=%d",
               (unsigned)round, (unsigned)core, erase_ok, write_ok, read_ok,
               data_ok);
  }
  test_assert(expected);

  sys_atomic_inc(&_rounds);
}

static void on_poll(void) {
  if (sys_atomic_get(&_done) != 0u) {
    return;
  }

  // Runs the full round count regardless of whether core 1 has already
  // been seen - stopping as soon as it's seen once would under-exercise
  // the very thing this test is for: both cores actively calling flash
  // operations concurrently for a sustained stretch, not just once.
  if (sys_atomic_get(&_rounds) >= FLASH_EVENT_TEST_MAX_ROUNDS) {
    sys_atomic_set(&_done, 1);
    sys_runloop_shutdown(0);
  }
}

test_main_hw(0) {
  _block = hw_block_flash_init(FLASH_SECTOR_BYTES);
  test_assert(_block != NULL);

  sys_atomic_init(&_rounds, 0);
  sys_atomic_init(&_core1_seen, 0);
  sys_atomic_init(&_done, 0);
  sys_atomic_init(&_worker_ready, 0);

  _queue = sys_event_queue_init(8);
  test_assert(_queue != NULL);

  sys_timer_t *timer = sys_timer_init(20, on_timer, NULL);
  test_assert(timer != NULL);
  test_assert(sys_timer_start(timer));

  test_assert(sys_runloop_run(2, _queue, on_init, on_event, on_poll, NULL) ==
              0);

  sys_timer_deinit(timer);
  sys_event_queue_deinit(_queue);
  _queue = NULL;

  sys_printf("[hw_028] rounds=%u core1_seen=%d\n",
             (unsigned)sys_atomic_get(&_rounds),
             sys_atomic_get(&_core1_seen));

  // If every single round happened to land on core 0, this test proved
  // nothing about the core-1 path - fail loudly rather than silently
  // passing (the same "prove the test can actually observe the thing it
  // claims to" bar hw_023/hid_010 hold their own device-count checks to).
  test_assert(sys_atomic_get(&_core1_seen) != 0u);

  hw_block_deinit(_block);
}

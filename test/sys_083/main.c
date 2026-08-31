#include <picofuse/sys.h>
#include <test/test.h>

static sys_atomic_t _active_count;
static sys_atomic_t _overlap_detected;
static sys_atomic_t _fire_count;

static void slow_callback(sys_timer_t *timer) {
  (void)timer;
#if defined(SYSTEM_NAME_PICO)
  // Confirms the cross-core alarm pool design: a timer started from core 1
  // must have its callback delivered on core 1 too.
  test_assert(sys_thread_core() == 1);
#endif

  // If another invocation is still in flight, this one should never have
  // started - the implementation must serialize/skip overlapping fires
  // rather than running them concurrently.
  if (sys_atomic_inc(&_active_count) != 1) {
    sys_atomic_set(&_overlap_detected, 1);
  }
  sys_atomic_inc(&_fire_count);

  sys_sleep_ms(150);

  sys_atomic_dec(&_active_count);
}

#if defined(SYSTEM_NAME_PICO)
// On Pico the timer callback runs synchronously inside an interrupt handler
// on whichever core owns its alarm pool (see sys/timer.h's platform note),
// which blocks all other foreground code on that same core for the
// callback's full duration. Starting (and thus binding) the timer on core 1
// keeps the main thread on core 0 free to actually observe a callback
// mid-flight and to exercise cross-core sys_timer_deinit().
static void start_on_core1(void *arg) {
  sys_printf("DEBUG start_on_core1 entered, core=%u\n", sys_thread_core());
  sys_timer_t *timer = (sys_timer_t *)arg;
  bool ok = sys_timer_start(timer);
  sys_printf("DEBUG sys_timer_start returned %d\n", (int)ok);
  test_assert(ok);
}
#endif

test_main_sys() {
  sys_atomic_init(&_active_count, 0);
  sys_atomic_init(&_overlap_detected, 0);
  sys_atomic_init(&_fire_count, 0);

  ///////////////////////////////////////////////////////////////////////////
  // A callback interval much shorter than the callback's own execution time
  // must never run two invocations concurrently, and must not queue up a
  // backlog of skipped fires either.

  sys_timer_t *t = sys_timer_init(20, slow_callback, NULL);
  test_assert(t != NULL);

#if defined(SYSTEM_NAME_PICO)
  test_assert(sys_thread_create_on_core(start_on_core1, t, 1));
#else
  test_assert(sys_timer_start(t));
#endif

  // Over ~500ms with a 20ms interval there would be ~25 fires if none were
  // skipped, but each callback takes 150ms, so genuinely serialized firing
  // caps this at roughly 500/150 =~ 3-4.
  sys_sleep_ms(500);
  test_assert(sys_atomic_get(&_overlap_detected) == 0);
  test_assert(sys_atomic_get(&_fire_count) < 10);

  ///////////////////////////////////////////////////////////////////////////
  // sys_timer_deinit() must block until an in-flight callback has actually
  // finished before returning - not just stop future firings. Poll for a
  // callback to actually be mid-flight (its 150ms sleep) before deiniting,
  // so this isn't a timing guess.

  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&_active_count) == 0) {
    test_assert(sys_timestamp_ms() - start < 5000);
    sys_sleep_ms(1);
  }

  sys_timer_deinit(t);
  test_assert(sys_atomic_get(&_active_count) == 0);

}

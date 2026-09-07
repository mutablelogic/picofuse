#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_watchdog_t is a singleton (one hardware watchdog, unlike LED's pool of
// handles), and only has a real backend on Pico right now - Linux and
// Darwin both resolve to the stub (see hw/stub/watchdog.c), where every
// entry point is a well-defined no-op/NULL. This test tolerates that
// rather than asserting real hardware behavior on every platform.
test_main_hw(0) {
  // NULL-safety: every operation must tolerate an invalid handle.
  test_assert(hw_watchdog_did_reset(NULL) == false);
  hw_watchdog_enable(NULL, true);  // must not crash
  hw_watchdog_enable(NULL, false); // must not crash
  hw_watchdog_reset(NULL, 100);    // must not crash
  hw_watchdog_deinit(NULL);        // must not crash

  hw_watchdog_t *watchdog = hw_watchdog_init();
  if (watchdog == NULL) {
    sys_printf("[hw_021] no watchdog backend available on this platform\n");
    test_assert(hw_watchdog_maxtimeout_ms() == 0u);
    return;
  }

  uint32_t max_timeout_ms = hw_watchdog_maxtimeout_ms();
  sys_printf("[hw_021] watchdog: maxtimeout_ms=%u did_reset=%s\n",
             max_timeout_ms, hw_watchdog_did_reset(watchdog) ? "yes" : "no");
  test_assert(max_timeout_ms > 0u);

  // Enable feeding mode and pump hw_poll() a few times - must not reset the
  // board (the ping interval is a fraction of max_timeout_ms, so a handful
  // of short sleeps stays well inside it).
  hw_watchdog_enable(watchdog, true);
  for (int i = 0; i < 5; i++) {
    hw_poll();
    sys_sleep_ms(50);
  }
  hw_watchdog_enable(watchdog, false);

  hw_watchdog_deinit(watchdog);

  // The singleton is free again once deinited - a fresh init must succeed.
  hw_watchdog_t *watchdog2 = hw_watchdog_init();
  test_assert(watchdog2 != NULL);
  hw_watchdog_deinit(watchdog2);
}

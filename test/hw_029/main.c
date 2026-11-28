#include <hardware/structs/watchdog.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_watchdog_did_reset() on a Pico, across a restart, with the stage in watchdog
// scratch register 0, which is the program's. Run by hand, as the test runner doesn't
// see RTT after the restart: 0x68773202 is in the register once it has finished.
//   1. The watchdog is enabled and not fed, so it times out. If the last run
//      finished, its watchdog was disabled, so this boot wasn't the watchdog's, even
//      though the reason register still says the watchdog reset it.
//   2. The timeout restarted it. The watchdog is disabled, and it's finished.
#define STAGE_MAGIC 0x68773200u // "hw2" and the stage
#define STAGE_MASK 0xffffff00u
#define STAGE_TIMEOUT 1u
#define STAGE_FINISHED 2u

static uint32_t stage(void) {
  uint32_t value = watchdog_hw->scratch[0];
  return (value & STAGE_MASK) == STAGE_MAGIC ? value & ~STAGE_MASK : 0u;
}

static void set_stage(uint32_t value) { watchdog_hw->scratch[0] = STAGE_MAGIC | value; }

int main(void) {
  sys_init(0, NULL, 0, TEST_STDIO);
  hw_init();
  hw_watchdog_t *watchdog = hw_watchdog_init();
  test_assert(watchdog != NULL);
  bool did_reset = hw_watchdog_did_reset(watchdog);
  uint32_t last = stage();
  sys_printf("[hw_029] stage %u, did_reset=%s\n", (unsigned)last, did_reset ? "yes" : "no");

  if (last != STAGE_TIMEOUT) {
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());
    if (last == STAGE_FINISHED) {
      test_assert(!did_reset);
    }
    set_stage(STAGE_TIMEOUT);
    hw_watchdog_enable(watchdog, true);
    for (;;) {
      sys_sleep_ms(100); // not polled, so not fed
    }
  }

  test_assert(did_reset);
  hw_watchdog_deinit(watchdog);
  set_stage(STAGE_FINISHED);
  sys_printf("[TEST] [EXIT] %s\n", sys_env_name());
  hw_exit();
  sys_exit();
  return 0;
}

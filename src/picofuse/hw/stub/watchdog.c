#include <picofuse/hw.h>

struct hw_watchdog_t {
  uint8_t placeholder;
};

void _hw_watchdog_module_exit(void) {}

void _hw_watchdog_poll(void) {}

hw_watchdog_t *hw_watchdog_init(void) { return NULL; }

hw_watchdog_t *hw_watchdog_init_device(const char *device) {
  (void)device;
  return NULL;
}

void hw_watchdog_deinit(hw_watchdog_t *watchdog) { (void)watchdog; }

uint32_t hw_watchdog_maxtimeout_ms(void) { return 0u; }

bool hw_watchdog_did_reset(hw_watchdog_t *watchdog) {
  (void)watchdog;
  return false;
}

void hw_watchdog_enable(hw_watchdog_t *watchdog, bool enable) {
  (void)watchdog;
  (void)enable;
}

void hw_watchdog_reset(hw_watchdog_t *watchdog, uint32_t delay_ms) {
  (void)watchdog;
  (void)delay_ms;
}

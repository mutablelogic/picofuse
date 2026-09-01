#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Argument validation: an unsupported/nonexistent bank must be rejected
// rather than silently behaving like bank 0, and a NULL handle must be
// safe to pass to hw_gpio_deinit() (a no-op, not a crash). Bank 1 is
// unsupported on Pico's single-bank hardware, and on the host backends
// there's normally no /dev/gpiochip1 (Linux) or no backend at all
// (darwin), so hw_gpio_count(1) reporting 0 and hw_gpio_init(1, ...)
// failing holds on every platform this runs on - no platform guard needed.
test_main_hw(0) {
  test_assert(hw_gpio_count(1) == 0);
  test_assert(hw_gpio_init(1, 0, hw_gpio_output) == NULL);

  // Must not crash.
  hw_gpio_deinit(NULL);
}

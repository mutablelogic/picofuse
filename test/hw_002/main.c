#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Argument validation: an unsupported/nonexistent bank must be rejected
// rather than silently behaving like bank 0, and a NULL handle must be
// safe to pass to hw_gpio_deinit() (a no-op, not a crash). Bank 1 isn't
// a safe choice here - real Raspberry Pi hardware commonly exposes a
// second chip (e.g. /dev/gpiochip1 for the firmware-backed
// raspberrypi-exp-gpio expander), so hw_gpio_count(1) can genuinely be
// nonzero there. Bank 255 has no real backend on any platform this runs
// on, so it stays reliably rejected everywhere.
test_main_hw(0) {
  test_assert(hw_gpio_count(255) == 0);
  test_assert(hw_gpio_init(255, 0, hw_gpio_output) == NULL);

  // Must not crash.
  hw_gpio_deinit(NULL);
}

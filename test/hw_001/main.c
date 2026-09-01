#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Basic hw_gpio_* lifecycle: init as output, check pin num/mode, set/get
// round-trip, then cycle set_mode()/get_mode() through input/pullup/
// pulldown. Only get_mode() is checked for those three - the resulting
// logic level on a "floating" pin depends on whatever else is wired to it
// on a given board (e.g. an external pull-up not documented in the board
// header can beat the RP2350's own weak internal pulldown), so it isn't a
// safe thing to assert here. Bank 0, pin 0 is unused by any board default
// (UART, I2C, SPI) on every board this runs on. Modes that reassign the
// pin's function entirely (spi/i2c/uart/pwm/adc) aren't exercised here:
// they don't have plain get/set semantics, and uart specifically risks
// fighting over the pin mux with the platform's own console UART. If the
// platform has no GPIO backend available at all (e.g. no /dev/gpiochipN in
// a container with no device passthrough), hw_gpio_count() reports 0 and
// there's nothing to exercise.
test_main_hw(0) {
  uint8_t bank = 0;
  uint8_t pin = 0;

  uint8_t count = hw_gpio_count(bank);
  sys_debugf("hw_001", "hw_gpio_count(%u) = %u", bank, count);
  if (count == 0) {
    return;
  }

  hw_gpio_t *gpio = hw_gpio_init(bank, pin, hw_gpio_output);
  test_assert(gpio != NULL);
  test_assert(hw_gpio_get_pin_num(gpio) == pin);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_output);

  hw_gpio_set(gpio, true);
  test_assert(hw_gpio_get(gpio) == true);

  hw_gpio_set(gpio, false);
  test_assert(hw_gpio_get(gpio) == false);

  hw_gpio_set_mode(gpio, hw_gpio_pullup);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_pullup);

  hw_gpio_set_mode(gpio, hw_gpio_pulldown);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_pulldown);

  hw_gpio_set_mode(gpio, hw_gpio_input);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_input);

  // Back to output before deinit.
  hw_gpio_set_mode(gpio, hw_gpio_output);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_output);

  hw_gpio_deinit(gpio);
}

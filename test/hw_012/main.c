#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

#define HW_LED_TEST_GPIO_PIN 0

// hw_led_init_gpio() NULL-safety plus its real on/off/brightness lifecycle
// against a single GPIO pin - no LED needs to actually be attached, this
// verifies the pin's own level, not anything visual. Skips cleanly if
// this platform has no real GPIO backend at all (Darwin's stub, which
// always returns NULL from hw_gpio_init()).
test_main_hw(0) {
  test_assert(hw_led_init_gpio(NULL) == NULL);

  hw_gpio_t *gpio = hw_gpio_init(0, HW_LED_TEST_GPIO_PIN, hw_gpio_none);
  if (gpio == NULL) {
    sys_printf("[hw_012] no GPIO backend available on this platform\n");
    return;
  }

  // A second init on the same pin, while the first handle is still open,
  // is rejected rather than silently stealing it.
  test_assert(hw_gpio_init(0, HW_LED_TEST_GPIO_PIN, hw_gpio_none) == NULL);

  hw_led_t *led = hw_led_init_gpio(gpio);
  test_assert(led != NULL);

  test_assert(hw_led_set(led, 0, true));
  sys_sleep_ms(50);
  test_assert(hw_gpio_get(gpio) == true);

  test_assert(hw_led_set(led, 0, false));
  sys_sleep_ms(50);
  test_assert(hw_gpio_get(gpio) == false);

  // GPIO has no intermediate level - any nonzero brightness is just on.
  test_assert(hw_led_set_brightness(led, 0, 50.0f));
  test_assert(hw_gpio_get(gpio) == true);
  test_assert(hw_led_set_brightness(led, 0, 0.0f));
  test_assert(hw_gpio_get(gpio) == false);

  test_assert(hw_led_clear(led));
  test_assert(hw_gpio_get(gpio) == false);

  hw_led_deinit(led);
  hw_gpio_deinit(gpio);

  // The pin is free again once deinited - a fresh init must succeed.
  hw_gpio_t *gpio2 = hw_gpio_init(0, HW_LED_TEST_GPIO_PIN, hw_gpio_none);
  test_assert(gpio2 != NULL);
  hw_gpio_deinit(gpio2);
}

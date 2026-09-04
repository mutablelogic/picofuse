#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

#define HW_LED_TEST_NEOPIXEL_PIN 0
#define HW_LED_TEST_NEOPIXEL_COUNT 3

// hw_led_init_neopixel() NULL-safety plus its real per-index on/off/
// brightness and whole-chain-clear lifecycle - no physical NeoPixel chain
// needs to be attached, this only exercises the driver/PIO logic (does it
// accept/reject the right things, does the flush not hang or fail).
test_main_hw(0) {
  test_assert(hw_led_init_neopixel(NULL, 1) == NULL);

  hw_gpio_t *gpio = hw_gpio_init(0, HW_LED_TEST_NEOPIXEL_PIN, hw_gpio_none);
  test_assert(gpio != NULL);

  test_assert(hw_led_init_neopixel(gpio, 0) == NULL); // led_count == 0

  hw_led_t *led = hw_led_init_neopixel(gpio, HW_LED_TEST_NEOPIXEL_COUNT);
  test_assert(led != NULL);

  // Per-index on/off.
  for (uint8_t i = 0; i < HW_LED_TEST_NEOPIXEL_COUNT; i++) {
    test_assert(hw_led_set(led, i, true));
    sys_sleep_ms(50);
  }
  test_assert(hw_led_set(led, 0, false));

  // Out-of-range index is rejected, not clamped.
  test_assert(hw_led_set(led, HW_LED_TEST_NEOPIXEL_COUNT, true) == false);
  test_assert(
      hw_led_set_brightness(led, HW_LED_TEST_NEOPIXEL_COUNT, 50.0f) == false);

  // Per-index brightness, independent of the other pixels.
  test_assert(hw_led_set_brightness(led, 1, 50.0f));
  test_assert(hw_led_set_brightness(led, 2, 100.0f));
  sys_sleep_ms(50);

  // Out-of-range brightness is clamped, not rejected.
  test_assert(hw_led_set_brightness(led, 1, -10.0f));
  test_assert(hw_led_set_brightness(led, 1, 150.0f));

  // Unlike _set(), which only ever touches one index, hw_led_clear() turns
  // off every LED in the chain (see the public API doc).
  test_assert(hw_led_clear(led));

  hw_led_deinit(led);

  // hw_led_init_neopixel() never takes ownership of the gpio handle it's
  // given - the caller releases it, same as hw_led_init_gpio()/_pwm().
  hw_gpio_deinit(gpio);

  // The pin/PIO resources are free again once deinited - a fresh init
  // must succeed.
  hw_gpio_t *gpio2 = hw_gpio_init(0, HW_LED_TEST_NEOPIXEL_PIN, hw_gpio_none);
  test_assert(gpio2 != NULL);
  hw_led_t *led2 = hw_led_init_neopixel(gpio2, HW_LED_TEST_NEOPIXEL_COUNT);
  test_assert(led2 != NULL);
  hw_led_deinit(led2);
  hw_gpio_deinit(gpio2);
}

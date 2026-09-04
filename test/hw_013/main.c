#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

#define HW_LED_TEST_PWM_PIN 0

// hw_led_init_pwm() NULL-safety plus its real on/off/brightness lifecycle
// against a single PWM channel - no LED needs to actually be attached,
// this verifies the channel's own duty/enabled state, not anything
// visual. Skips cleanly if this platform has no GPIO-based PWM backend:
// Linux's hw_pwm_init() is a stub (only hw_pwm_init_device() is real
// there - see hw/linux/pwm.c), and Darwin has no PWM backend at all.
test_main_hw(0) {
  test_assert(hw_led_init_pwm(NULL) == NULL);

  hw_gpio_t *gpio = hw_gpio_init(0, HW_LED_TEST_PWM_PIN, hw_gpio_none);
  if (gpio == NULL) {
    sys_printf("[hw_013] no GPIO backend available on this platform\n");
    return;
  }

  hw_pwm_t *pwm = hw_pwm_init(gpio, NULL, NULL, NULL);
  if (pwm == NULL) {
    sys_printf(
        "[hw_013] no GPIO-based PWM backend available on this platform\n");
    hw_gpio_deinit(gpio);
    return;
  }

  hw_led_t *led = hw_led_init_pwm(pwm);
  test_assert(led != NULL);

  test_assert(hw_led_set(led, 0, true));
  sys_sleep_ms(50);
  test_assert(hw_pwm_get_enabled(pwm) == true);
  test_assert(hw_pwm_get_duty_percent(pwm) > 99.0f);

  test_assert(hw_led_set(led, 0, false));
  test_assert(hw_pwm_get_enabled(pwm) == false);
  test_assert(hw_pwm_get_duty_percent(pwm) == 0.0f);

  // Brightness sweep, checked against the underlying PWM channel's own
  // duty/enabled state, with the same quantization tolerance hw_010 uses
  // for hw_pwm_set_duty_percent() directly.
  float levels[] = {25.0f, 50.0f, 75.0f};
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    test_assert(hw_led_set_brightness(led, 0, levels[i]));
    float actual = hw_pwm_get_duty_percent(pwm);
    sys_debugf("hw_013", "brightness requested=%f actual=%f",
               (double)levels[i], (double)actual);
    test_assert(actual >= levels[i] - 1.0f && actual <= levels[i] + 1.0f);
    test_assert(hw_pwm_get_enabled(pwm) == true);
    sys_sleep_ms(50);
  }

  test_assert(hw_led_set_brightness(led, 0, 0.0f));
  test_assert(hw_pwm_get_enabled(pwm) == false);

  test_assert(hw_led_clear(led));
  test_assert(hw_pwm_get_enabled(pwm) == false);

  hw_led_deinit(led);

  // hw_led_init_pwm() never takes ownership of the pwm/gpio handles it's
  // given - the caller releases them, same as hw_led_init_gpio().
  hw_pwm_deinit(pwm);
  hw_gpio_deinit(gpio);
}

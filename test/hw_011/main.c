#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_led_blink()'s own flips only ever land via hw_poll() (see
// blink.c's own doc: its timer callback just queues one up, since a
// backend's ops->set() isn't safe to call from the timer's own ISR
// context) - a plain sys_sleep_ms() never pumps that, the same reason
// hw_017's own wait_for() has to poll instead of sleeping blindly.
static void sleep_and_poll(uint32_t ms) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < ms) {
    hw_poll();
    sys_sleep_ms(10);
  }
}

static const char *_led_type_name(hw_led_type_t type) {
  switch (type) {
  case hw_led_type_gpio:
    return "gpio";
  case hw_led_type_pwm:
    return "pwm";
  case hw_led_type_wifi:
    return "wifi";
  case hw_led_type_neopixel:
    return "neopixel";
  case hw_led_type_none:
  default:
    return "none";
  }
}

// hw_led_* NULL-safety, plus the default on-board LED's on/off/brightness/
// clear lifecycle - whatever type it actually resolves to (GPIO, PWM,
// Wi-Fi, or NeoPixel) on this board. Note hw_gpio_init()/hw_led_init_wifi()
// don't currently guard against a pin/GPIO already being claimed the way
// SPI/I2C/PWM's pools do, so this doesn't test double-init exclusivity -
// there isn't any yet.
//
// hw_led_init_wifi() (and so hw_led_init_default(), on Wi-Fi LED boards)
// requires the CYW43 driver already initialized - test_main_hw()'s own
// hw_init() already does that on CYW43-capable boards (see
// hw/pico/init.c), so there's nothing extra to bring up here.
//
// If no default LED is available at all, this exits cleanly rather than
// failing - there's nothing to exercise, not a test failure.
test_main_hw(0) {
  // NULL-safety: every operation must tolerate an invalid handle.
  test_assert(hw_led_set(NULL, 0, true) == false);
  test_assert(hw_led_set_brightness(NULL, 0, 50.0f) == false);
  test_assert(hw_led_clear(NULL) == false);
  test_assert(hw_led_blink(NULL, 0, 100, false) == false);
  hw_led_deinit(NULL); // must not crash

  hw_led_type_t type = hw_led_type_none;
  uint8_t count = 0;
  uint8_t pin = hw_led_gpio_default(&type, &count);
  sys_printf("[hw_011] default LED: type=%s pin=%u count=%u\n",
             _led_type_name(type), pin, count);

  hw_led_t *led = hw_led_init_default();
  if (led == NULL) {
    sys_printf("[hw_011] no default LED available on this board\n");
    return;
  }

  // On/off round trip.
  test_assert(hw_led_set(led, 0, true));
  sys_sleep_ms(300);
  test_assert(hw_led_set(led, 0, false));
  sys_sleep_ms(300);

  // Brightness sweep - always accepted, even for GPIO/Wi-Fi LED types,
  // which just threshold it to on/off (see hw_led_set_brightness()'s doc).
  float levels[] = {0.0f, 25.0f, 50.0f, 75.0f, 100.0f};
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    test_assert(hw_led_set_brightness(led, 0, levels[i]));
    sys_sleep_ms(200);
  }

  // Out-of-range brightness is clamped, not rejected.
  test_assert(hw_led_set_brightness(led, 0, -10.0f));
  test_assert(hw_led_set_brightness(led, 0, 150.0f));

  test_assert(hw_led_clear(led));

  // hw_led_blink(): starts, and calling it again while already active
  // cancels the first blink and starts the new one rather than failing
  // (only one blink can ever be active per handle - see hw_led_blink()'s
  // own doc on why); hw_led_set()/hw_led_clear() also cancel one.
  test_assert(hw_led_blink(led, 0, 100, true));
  test_assert(hw_led_blink(led, 0, 100, true)); // cancels + restarts
  sleep_and_poll(350); // several flips
  test_assert(hw_led_clear(led)); // cancels the repeating blink
  test_assert(hw_led_blink(led, 0, 100, true)); // slot is free again
  test_assert(hw_led_set(led, 0, false)); // cancels it too

  // Non-repeating: one flip (off -> on) after one period, then the timer
  // stops itself - a fresh hw_led_blink() succeeds again once that's had
  // time to happen, with no hw_led_set()/hw_led_clear() needed.
  test_assert(hw_led_blink(led, 0, 100, false));
  sleep_and_poll(250); // past the single flip
  test_assert(hw_led_blink(led, 0, 100, true));
  test_assert(hw_led_clear(led));

  hw_led_deinit(led);

  // The slot/resource is free again once deinited - a fresh init must
  // succeed.
  hw_led_t *led2 = hw_led_init_default();
  test_assert(led2 != NULL);
  hw_led_deinit(led2);
}

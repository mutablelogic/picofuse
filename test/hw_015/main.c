#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_led_init_device() NULL-safety plus its real on/off/brightness
// lifecycle against a real Linux LED-class entry, if one named "PWR"
// exists on this system (common on Raspberry Pi boards, alongside "ACT").
// Skips cleanly otherwise - Pico/Darwin have no LED-class concept at all
// (always NULL, see hw/led/led_device.c's stub), and so does any Linux
// system without a "PWR" LED.
test_main_hw(0) {
  test_assert(hw_led_init_device(NULL) == NULL);
  test_assert(hw_led_init_device("") == NULL);

  hw_led_t *led = hw_led_init_device("PWR");
  if (led == NULL) {
    sys_printf("[hw_015] no \"PWR\" LED available on this system\n");
    return;
  }

  test_assert(hw_led_set(led, 0, true));
  sys_sleep_ms(300);
  test_assert(hw_led_set(led, 0, false));
  sys_sleep_ms(300);

  float levels[] = {0.0f, 25.0f, 50.0f, 75.0f, 100.0f};
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    test_assert(hw_led_set_brightness(led, 0, levels[i]));
    sys_sleep_ms(200);
  }

  test_assert(hw_led_clear(led));

  hw_led_deinit(led);

  // The resource is free again once deinited - a fresh init must succeed.
  hw_led_t *led2 = hw_led_init_device("PWR");
  test_assert(led2 != NULL);
  hw_led_deinit(led2);
}

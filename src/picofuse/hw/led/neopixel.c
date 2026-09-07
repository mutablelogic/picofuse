#include "led.h"

/** Stub implementation for Linux and Darwin */
hw_led_t *hw_led_init_neopixel(hw_gpio_t *gpio, uint8_t led_count) {
  (void)gpio;
  (void)led_count;
  return NULL;
}

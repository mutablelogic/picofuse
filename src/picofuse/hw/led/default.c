#include "led.h"

/** Stub implementation for Linux and Darwin */
uint8_t hw_led_gpio_default(hw_led_type_t *out_type, uint8_t *out_count) {
  if (out_type != NULL) {
    *out_type = hw_led_type_none;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  return HW_LED_GPIO_NONE;
}

/** Stub implementation for Linux and Darwin */
hw_led_t *hw_led_init_default(void) { return NULL; }

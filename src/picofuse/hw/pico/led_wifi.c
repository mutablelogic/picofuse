#include "../led/led.h"
#include <picofuse/sys.h>

#ifdef PICO_CYW43_SUPPORTED
#include "pico/cyw43_arch.h"

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct {
  uint8_t pin;
} _hw_led_wifi_ctx_t;

_Static_assert(sizeof(_hw_led_wifi_ctx_t) <= HW_LED_CONTEXT_SIZE,
              "_hw_led_wifi_ctx_t exceeds HW_LED_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_led_wifi_set(hw_led_t *led, uint8_t index, bool enabled) {
  (void)index; // a single Wi-Fi GPIO has no addressable sub-index
  _hw_led_wifi_ctx_t *ctx = _hw_led_context(led);
  cyw43_arch_gpio_put(ctx->pin, enabled);
  return true;
}

static bool _hw_led_wifi_clear(hw_led_t *led) {
  return _hw_led_wifi_set(led, 0, false);
}

// A CYW43 GPIO has no intermediate level - any nonzero brightness is just
// "on".
static bool _hw_led_wifi_set_brightness(hw_led_t *led, uint8_t index,
                                        float percent) {
  return _hw_led_wifi_set(led, index, percent > 0.0f);
}

// No deinit - bringing up/tearing down the CYW43 driver itself
// (cyw43_arch_init()/deinit()) is the application's responsibility, same
// as it owns whether Wi-Fi is running at all; this backend only ever
// toggles one of its GPIOs.
static const hw_led_ops_t _hw_led_wifi_ops = {
    .set = _hw_led_wifi_set,
    .set_brightness = _hw_led_wifi_set_brightness,
    .clear = _hw_led_wifi_clear,
    .deinit = NULL,
};
#endif // PICO_CYW43_SUPPORTED

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_led_t *hw_led_init_wifi(void) {
  sys_debugf("hw", "led_init_wifi");
#ifdef PICO_CYW43_SUPPORTED
  hw_led_type_t led_type = hw_led_type_none;
  uint8_t led_pin = hw_led_gpio_default(&led_type, NULL);
  if (!cyw43_is_initialized(&cyw43_state)) {
    sys_debugf("hw", "led_init_wifi: cyw43 not initialized");
    return NULL;
  }
  if (led_type != hw_led_type_wifi || led_pin == HW_LED_GPIO_NONE) {
    sys_debugf("hw", "led_init_wifi: no wifi LED pin");
    return NULL;
  }

  hw_led_t *led = _hw_led_alloc(&_hw_led_wifi_ops);
  if (led == NULL) {
    return NULL;
  }

  _hw_led_wifi_ctx_t *ctx = _hw_led_context(led);
  ctx->pin = led_pin;
  return led;
#else
  return NULL;
#endif
}

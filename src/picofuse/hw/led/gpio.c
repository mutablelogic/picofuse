#include "led.h"
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct {
  hw_gpio_t *gpio;
} _hw_led_gpio_ctx_t;

_Static_assert(sizeof(_hw_led_gpio_ctx_t) <= HW_LED_CONTEXT_SIZE,
               "_hw_led_gpio_ctx_t exceeds HW_LED_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_led_gpio_set(hw_led_t *led, uint8_t index, bool enabled) {
  (void)index; // a single GPIO has no addressable sub-index
  _hw_led_gpio_ctx_t *ctx = _hw_led_context(led);
  hw_gpio_set(ctx->gpio, enabled);
  return true;
}

static bool _hw_led_gpio_clear(hw_led_t *led) {
  return _hw_led_gpio_set(led, 0, false);
}

// A plain GPIO has no intermediate level - any nonzero brightness is just
// "on".
static bool _hw_led_gpio_set_brightness(hw_led_t *led, uint8_t index,
                                        float percent) {
  return _hw_led_gpio_set(led, index, percent > 0.0f);
}

static const hw_led_ops_t _hw_led_gpio_ops = {
    .set = _hw_led_gpio_set,
    .set_brightness = _hw_led_gpio_set_brightness,
    .clear = _hw_led_gpio_clear,
    .deinit = NULL,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_led_t *hw_led_init_gpio(hw_gpio_t *gpio) {
  if (gpio == NULL) {
    return NULL;
  }

  hw_led_t *led = _hw_led_alloc(&_hw_led_gpio_ops);
  if (led == NULL) {
    return NULL;
  }

  _hw_led_gpio_ctx_t *ctx = _hw_led_context(led);
  ctx->gpio = gpio;

  hw_gpio_set_mode(gpio, hw_gpio_output);
  hw_gpio_set(gpio, false); // start off, matching hw_led_init_pwm()'s doc

  return led;
}

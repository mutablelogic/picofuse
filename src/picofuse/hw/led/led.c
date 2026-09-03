#include "led.h"
#include <picofuse/sys.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief LED handle (shared across all backends).
 *
 * Deliberately private to this file - a backend never sees this layout,
 * only the `ops`/`context` pair it handed to `_hw_led_construct()` (see
 * `_hw_led_context()`).
 */
struct hw_led_t {
  const hw_led_ops_t *ops;
  void *context;
};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief A handle only counts as usable once init has actually bound it to
 * a backend - ops/context are set together, by whichever hw_led_init_*()
 * built it, never independently. */
static inline bool _hw_led_valid(const hw_led_t *led) {
  return led != NULL && led->ops != NULL && led->context != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS (see ../led.h)

hw_led_t *_hw_led_construct(const hw_led_ops_t *ops, void *context) {
  if (ops == NULL) {
    return NULL;
  }

  hw_led_t *led = sys_malloc(sizeof(hw_led_t));
  if (led == NULL) {
    return NULL;
  }

  led->ops = ops;
  led->context = context;
  return led;
}

void *_hw_led_context(const hw_led_t *led) {
  return led != NULL ? led->context : NULL;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE
//
// No LED backend (GPIO, NeoPixel, Wi-Fi, PWM) is wired up yet, so every
// hw_led_init_*() below is a stub that returns NULL. hw_led_deinit() is
// already generic - it dispatches through whichever ops table a future
// backend constructs its handle with.

/** Stub implementation: no LED backend wired up yet. */
hw_led_t *hw_led_init_gpio(hw_gpio_t *gpio) {
  (void)gpio;
  return NULL;
}

/** Stub implementation: no LED backend wired up yet. */
hw_led_t *hw_led_init_neopixel(hw_gpio_t *gpio, uint8_t led_count) {
  (void)gpio;
  (void)led_count;
  return NULL;
}

/** Stub implementation: no LED backend wired up yet. */
hw_led_t *hw_led_init_wifi(void) { return NULL; }

/** Stub implementation: no LED backend wired up yet. */
hw_led_t *hw_led_init_pwm(hw_pwm_t *pwm) {
  (void)pwm;
  return NULL;
}

/** Stub implementation: no LED backend wired up yet. */
hw_led_t *hw_led_init_default(void) { return NULL; }

void hw_led_deinit(hw_led_t *led) {
  if (!_hw_led_valid(led) || led->ops->deinit == NULL) {
    return;
  }
  led->ops->deinit(led);
  sys_free(led);
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** Stub implementation: no default on-board LED is available yet. */
uint8_t hw_led_gpio_default(hw_led_type_t *out_type, uint8_t *out_count) {
  if (out_type != NULL) {
    *out_type = hw_led_type_none;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  return HW_LED_GPIO_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_led_set(hw_led_t *led, uint8_t index, bool enabled) {
  if (!_hw_led_valid(led) || led->ops->set == NULL) {
    return false;
  }
  return led->ops->set(led, index, enabled);
}

bool hw_led_clear(hw_led_t *led) {
  if (!_hw_led_valid(led) || led->ops->clear == NULL) {
    return false;
  }
  return led->ops->clear(led);
}

/** Stub implementation: blinking is not implemented yet. */
bool hw_led_blink(hw_led_t *led, uint8_t index, uint32_t period_ms,
                  bool repeating) {
  (void)led;
  (void)index;
  (void)period_ms;
  (void)repeating;
  return false;
}

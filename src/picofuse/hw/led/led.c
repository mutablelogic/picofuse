#include "private.h"
#include <stddef.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Not static - blink.c's own _hw_led_poll() iterates this too (see
// private.h's own extern declaration).
hw_led_t _hw_led_pool[HW_LED_POOL_CAPACITY] = {0};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS (see led.h)

/** @brief Claims a free slot from the static instance pool, or NULL if
 * every slot is already in use. */
static inline hw_led_t *_hw_led_pool_claim(void) {
  for (size_t i = 0; i < HW_LED_POOL_CAPACITY; i++) {
    if (_hw_led_pool[i].ops == NULL) {
      return &_hw_led_pool[i];
    }
  }
  return NULL;
}

hw_led_t *_hw_led_alloc(const hw_led_ops_t *ops) {
  if (ops == NULL) {
    return NULL;
  }

  _HW_LED_LOCK();
  hw_led_t *led = _hw_led_pool_claim();
  if (led != NULL) {
    memset(led->context, 0, sizeof(led->context));
    memset(&led->blink, 0, sizeof(led->blink));
    led->ops = ops;
  }
  _HW_LED_UNLOCK();
  return led;
}

void *_hw_led_context(const hw_led_t *led) {
  return _hw_led_valid(led) ? (void *)led->context : NULL;
}

pix_color_t _hw_led_get_color(hw_led_t *led, uint8_t index) {
  if (!_hw_led_valid(led) || led->ops->get_color == NULL) {
    return PIX_COLOR_BLACK;
  }
  return led->ops->get_color(led, index);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void hw_led_deinit(hw_led_t *led) {
  if (!_hw_led_valid(led)) {
    return;
  }
  // Stop any active blink timer before the pool slot beneath it goes away
  // - otherwise it would keep firing into a handle that's since been
  // reused for a different LED.
  _hw_led_blink_cancel(led);
  // Leave the LED off rather than however it happened to be left, before
  // a backend releases whatever hardware was driving it.
  if (led->ops->clear != NULL) {
    led->ops->clear(led);
  }
  if (led->ops->deinit != NULL) {
    led->ops->deinit(led);
  }
  // Release the pool slot back
  _HW_LED_LOCK();
  led->ops = NULL;
  _HW_LED_UNLOCK();
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_led_set(hw_led_t *led, uint8_t index, bool enabled) {
  if (!_hw_led_valid(led) || led->ops->set == NULL) {
    return false;
  }
  // An explicit set() overrides whatever a blink was doing - see
  // hw_led_blink()'s own doc.
  _hw_led_blink_cancel(led);
  return led->ops->set(led, index, enabled);
}

bool hw_led_set_brightness(hw_led_t *led, uint8_t index, float percent) {
  if (!_hw_led_valid(led) || led->ops->set_brightness == NULL) {
    return false;
  }
  return led->ops->set_brightness(led, index, percent);
}

bool hw_led_set_color(hw_led_t *led, uint8_t index, pix_color_t color) {
  if (!_hw_led_valid(led)) {
    return false;
  }
  // An explicit set_color() overrides whatever a blink was doing - same
  // as hw_led_set()'s own doc.
  _hw_led_blink_cancel(led);

  if (led->ops->set_color != NULL) {
    return led->ops->set_color(led, index, color);
  }
  if (led->ops->set_brightness == NULL) {
    return false;
  }

  // No real color concept (GPIO/PWM/Wi-Fi/sysfs): fall back to perceived
  // brightness, weighted per ITU-R BT.601 luma (0.299/0.587/0.114,
  // rounded here to 0.2/0.7/0.1) rather than a plain average, since green
  // reads far brighter to the eye than red or blue at the same channel
  // value. Alpha scales the result the same way it scales a NeoPixel's
  // own channels (see led_neopixel.c's own _hw_led_neopixel_scale()).
  float luma = 0.2f * (float)pix_color_r(color) +
              0.7f * (float)pix_color_g(color) +
              0.1f * (float)pix_color_b(color);
  float percent = luma * (float)pix_color_a(color) / (255.0f * 255.0f) * 100.0f;
  return led->ops->set_brightness(led, index, percent);
}

bool hw_led_clear(hw_led_t *led) {
  if (!_hw_led_valid(led) || led->ops->clear == NULL) {
    return false;
  }
  // See hw_led_clear()'s own doc: "cancelling any active blink".
  _hw_led_blink_cancel(led);
  return led->ops->clear(led);
}

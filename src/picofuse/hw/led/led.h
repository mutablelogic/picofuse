#pragma once
#include <picofuse/hw.h>
#include <picofuse/pix/color.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Backend operations for an LED handle.
 *
 * @p set_color and @p get_color are optional (NULL when a backend has no
 * real color concept, e.g. GPIO/PWM/Wi-Fi/sysfs) - hw_led_set_color()
 * falls back to @p set_brightness for those (see its own doc), and
 * blink.c's own hw_led_blink() falls back to plain white when @p
 * get_color is NULL.
 *
 * @p get_color must report the color at full brightness (alpha 0xFF),
 * independent of whatever @p set_brightness may have separately scaled
 * it to - color and brightness are orthogonal properties from a caller's
 * point of view (that's the whole reason they're separate calls), and
 * hw_led_blink() relies on this to blink a dimmed pixel's true hue at
 * full brightness rather than however dim it happened to be left. See
 * _hw_led_neopixel_get_color()'s own doc for how NeoPixel - the only
 * backend that stores brightness and color in the same value - does this.
 */
typedef struct hw_led_ops_t {
  bool (*set)(hw_led_t *led, uint8_t index, bool enabled);
  bool (*set_brightness)(hw_led_t *led, uint8_t index, float percent);
  bool (*set_color)(hw_led_t *led, uint8_t index, pix_color_t color);
  pix_color_t (*get_color)(hw_led_t *led, uint8_t index);
  bool (*clear)(hw_led_t *led);
  void (*deinit)(hw_led_t *led);
} hw_led_ops_t;

/**
 * @brief Allocate a handle bound to a backend.
 */
hw_led_t *_hw_led_alloc(const hw_led_ops_t *ops);

/**
 * @brief Get a handle's embedded scratch context buffer.
 */
void *_hw_led_context(const hw_led_t *led);

/**
 * @brief Dispatch to a handle's own ops->get_color, if it has one.
 *
 * Not part of the public API (see hw_led_ops_t's own doc on why
 * get_color stays internal) - only used by led_default.c's own wrapper
 * (to forward through to whatever real backend it wraps) and blink.c
 * (which has direct ops access already and calls ops->get_color itself,
 * not through here).
 *
 * @return The color last applied via ops->set_color, or @ref
 * PIX_COLOR_BLACK for an invalid handle or a backend with no get_color.
 */
pix_color_t _hw_led_get_color(hw_led_t *led, uint8_t index);

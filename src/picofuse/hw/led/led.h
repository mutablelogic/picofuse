#pragma once
#include <picofuse/hw.h>
#include <picofuse/pix/color.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Backend operations for an LED handle.
 *
 * @p set_color is optional (NULL when a backend has no real color
 * concept, e.g. GPIO/PWM/Wi-Fi/sysfs) - hw_led_set_color() falls back to
 * @p set_brightness for those, see its own doc.
 */
typedef struct hw_led_ops_t {
  bool (*set)(hw_led_t *led, uint8_t index, bool enabled);
  bool (*set_brightness)(hw_led_t *led, uint8_t index, float percent);
  bool (*set_color)(hw_led_t *led, uint8_t index, pix_color_t color);
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

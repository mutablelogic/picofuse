#pragma once
#include <picofuse/hw.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Backend operations for an LED handle.
 *
 * An LED isn't backed by one fixed peripheral - it could be a plain GPIO
 * pin, a NeoPixel/WS2812 data pin, a CYW43 Wi-Fi GPIO, or a PWM output - so
 * each backend (see the src/picofuse/hw/led directory) implements this and
 * hands it to `_hw_led_construct()` at its own `hw_led_init_*()` time. Every
 * public
 * `hw_led_*` operation (see led/stub.c) dispatches through whichever table
 * the handle was constructed with.
 *
 * `hw_led_blink()` has no entry here - a timer-driven repeat works the same
 * regardless of backend, so it's implemented once on top of `set`/`clear`
 * instead of duplicated per backend.
 */
typedef struct hw_led_ops_t {
  bool (*set)(hw_led_t *led, uint8_t index, bool enabled);
  bool (*clear)(hw_led_t *led);
  void (*deinit)(hw_led_t *led);
} hw_led_ops_t;

/**
 * @brief Construct a handle bound to a backend and its private context.
 *
 * `hw_led_t`'s own layout stays private to led/stub.c - this is the only way
 * a backend gets to create one. Returns `NULL` if `ops` is `NULL` or
 * allocation fails.
 */
hw_led_t *_hw_led_construct(const hw_led_ops_t *ops, void *context);

/**
 * @brief Get a handle's backend-private context.
 * @return The same pointer passed to `_hw_led_construct()`, or `NULL` if
 * `led` is invalid.
 */
void *_hw_led_context(const hw_led_t *led);

#pragma once
#include <picofuse/hw/gpio.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Backend operations for a single GPIO pin.
 *
 * A pin on a host platform isn't a fixed hardware register - it's either
 * the OS's own native GPIO interface or a device sitting behind an FTDI
 * adapter - so each backend (native chardev, FTDI, ...) implements this and
 * hands it to `_hw_gpio_construct()` at `hw_gpio_init()` time. Every public
 * `hw_gpio_*` operation (see gpio.c) dispatches through whichever table the
 * handle was constructed with.
 */
typedef struct hw_gpio_ops_t {
  uint8_t (*get_pin_num)(hw_gpio_t *gpio);
  bool (*get)(hw_gpio_t *gpio);
  void (*set)(hw_gpio_t *gpio, bool value);
  hw_gpio_mode_t (*get_mode)(hw_gpio_t *gpio);
  void (*set_mode)(hw_gpio_t *gpio, hw_gpio_mode_t mode);
  void (*deinit)(hw_gpio_t *gpio);
} hw_gpio_ops_t;

/**
 * @brief Construct a handle bound to a backend and its private context.
 *
 * `hw_gpio_t`'s own layout stays private to gpio.c - this is the only way
 * a backend gets to create one. Returns `NULL` if `ops` is `NULL` or
 * allocation fails.
 */
hw_gpio_t *_hw_gpio_construct(const hw_gpio_ops_t *ops, void *context);

/**
 * @brief Get a handle's backend-private context.
 * @return The same pointer passed to `_hw_gpio_construct()`, or `NULL` if
 * `gpio` is invalid.
 */
void *_hw_gpio_context(const hw_gpio_t *gpio);

/**
 * @brief Invoke the process-wide GPIO interrupt callback, if one is set
 * (see `hw_gpio_set_callback()`).
 *
 * Lets a backend's own event-monitoring thread (e.g. the native chardev
 * backend's epoll loop) dispatch through the same callback/userdata pair
 * `hw_gpio_set_callback()` manages, without exposing that state itself.
 */
void _hw_gpio_dispatch_callback(uint8_t bank, uint8_t pin,
                                hw_gpio_event_t event);

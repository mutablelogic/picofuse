/**
 * @file gpio.h
 * @brief GPIO (General Purpose Input/Output) interface
 * @defgroup GPIO GPIO
 * @ingroup Hardware
 *
 * General Purpose Input/Output (GPIO) interface for hardware platforms.
 * This module provides functions to initialize GPIO pins, set their modes,
 * and handle interrupts.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief GPIO mode flags for configuring GPIO pins.
 * @ingroup GPIO
 */
typedef enum {
  hw_gpio_none = 0,
  hw_gpio_input,
  hw_gpio_pullup,
  hw_gpio_pulldown,
  hw_gpio_output,
  hw_gpio_spi,
  hw_gpio_i2c,
  hw_gpio_uart,
  hw_gpio_pwm,
  hw_gpio_adc,
  hw_gpio_unknown,
} hw_gpio_mode_t;

/**
 * @brief GPIO interrupt event flags.
 * @ingroup GPIO
 */
typedef enum {
  hw_gpio_rising = (1 << 0),
  hw_gpio_falling = (1 << 1),
} hw_gpio_event_t;

/**
 * @brief GPIO logical pin structure.
 * @ingroup GPIO
 * @headerfile gpio.h hw/hw.h
 */
typedef struct hw_gpio_t hw_gpio_t;

/**
 * @brief GPIO interrupt callback function pointer.
 * @ingroup GPIO
 *
 * @param bank The GPIO bank number that triggered the event.
 * @param pin The logical pin number that triggered the event.
 * @param event The type of GPIO event that occurred.
 * @param userdata User-defined data pointer passed when setting the callback.
 */
typedef void (*hw_gpio_callback_t)(uint8_t bank, uint8_t pin,
                                   hw_gpio_event_t event, void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a GPIO pin with the specified mode.
 * @ingroup GPIO
 *
 * @param bank The GPIO bank number to which the pin belongs.
 * @param pin The logical GPIO pin number to initialize.
 * @param mode The GPIO mode configuration.
 * @return A pointer to the initialized GPIO structure, or `NULL` if
 * initialization fails.
 */
hw_gpio_t *hw_gpio_init(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode);

/**
 * @brief Deinitialize and release a GPIO pin.
 * @ingroup GPIO
 *
 * @param gpio Pointer to the GPIO structure to deinitialize.
 */
void hw_gpio_deinit(hw_gpio_t *gpio);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Get the total number of available GPIO pins for a given bank.
 * @ingroup GPIO
 *
 * @param bank The GPIO bank number to query.
 * @return The number of GPIO pins available on the hardware platform, or
 * `0` if `bank` doesn't exist - or, on backends that can't always
 * determine this (e.g. an FTDI device with no known pin count for its
 * chip type), because the count is simply unknown.
 */
uint8_t hw_gpio_count(uint8_t bank);

/**
 * @brief Get the logical pin number for a GPIO handle.
 * @ingroup GPIO
 *
 * @param gpio Pointer to the GPIO structure.
 * @return The logical GPIO pin number.
 */
uint8_t hw_gpio_get_pin_num(const hw_gpio_t *gpio);

/**
 * @brief Set the global GPIO interrupt callback handler.
 * @ingroup GPIO
 *
 * @param callback Pointer to the callback function, or `NULL` to disable
 * interrupt handling.
 * @param userdata User-defined data pointer to pass to the callback.
 */
void hw_gpio_set_callback(hw_gpio_callback_t callback, void *userdata);

/**
 * @brief Get the current mode configuration of a GPIO pin.
 * @ingroup GPIO
 *
 * @param gpio Pointer to the GPIO structure.
 * @return The current GPIO mode configuration.
 */
hw_gpio_mode_t hw_gpio_get_mode(const hw_gpio_t *gpio);

/**
 * @brief Set the current mode configuration of a GPIO pin.
 * @ingroup GPIO
 *
 * @param gpio Pointer to the GPIO structure.
 * @param mode The new GPIO mode configuration to set.
 */
void hw_gpio_set_mode(hw_gpio_t *gpio, hw_gpio_mode_t mode);

/**
 * @brief Read the current state of a GPIO pin.
 * @ingroup GPIO
 *
 * @param gpio Pointer to the GPIO structure.
 * @retval true The pin is high.
 * @retval false The pin is low.
 */
bool hw_gpio_get(const hw_gpio_t *gpio);

/**
 * @brief Set the state of a GPIO pin.
 * @ingroup GPIO
 *
 * @param gpio Pointer to the GPIO structure.
 * @param value `true` to set the pin high, `false` to set it low.
 */
void hw_gpio_set(hw_gpio_t *gpio, bool value);

/** @} */

/**
 * @file stmpe610.h
 * @brief STMicroelectronics STMPE610 resistive touch controller interface.
 * @defgroup STMPE610 STMPE610
 * @ingroup Device
 *
 * This module provides a device-level API for STMPE610 resistive touch
 * controllers over SPI (e.g. Adafruit's 2.8" PiTFT resistive touch
 * display, which pairs it with an ILI9341 display controller on the same
 * SPI bus but a separate chip-select).
 */
#pragma once

#include <picofuse/hw.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque STMPE610 handle.
 * @ingroup STMPE610
 */
typedef struct dev_stmpe610_t dev_stmpe610_t;

/**
 * @brief Optional STMPE610 initialization options.
 * @ingroup STMPE610
 */
typedef struct {
  bool irq_active_low; ///< True when the interrupt pin is active low.
} dev_stmpe610_config_t;

/**
 * @brief Touch contact state reported by dev_stmpe610_poll().
 * @ingroup STMPE610
 */
typedef enum {
  dev_stmpe610_touch_up = 0,   ///< Contact lifted - no longer touching.
  dev_stmpe610_touch_down = 1, ///< A new contact.
  dev_stmpe610_touch_move = 2, ///< An existing contact moved.
} dev_stmpe610_touch_event_t;

/**
 * @brief The single touch contact reported by dev_stmpe610_poll() - unlike
 * a capacitive controller, STMPE610 is resistive and only ever reports one
 * point at a time.
 * @ingroup STMPE610
 */
typedef struct {
  dev_stmpe610_touch_event_t event; ///< Contact state for this sample.
  uint16_t x;                       ///< X coordinate, in raw ADC counts.
  uint16_t y;                       ///< Y coordinate, in raw ADC counts.
  uint8_t z;                        ///< Relative touch pressure/contact area.
} dev_stmpe610_touch_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Fill an STMPE610 config struct with safe defaults.
 * @ingroup STMPE610
 * @param config Config structure to initialize.
 */
void dev_stmpe610_default_config(dev_stmpe610_config_t *config);

/**
 * @brief Initialize an STMPE610 resistive touch controller over SPI.
 * @ingroup STMPE610
 * @param device SPI device handle from hw_spi_init() / hw_spi_init_default()
 * / hw_spi_init_device(), already opened on the controller's own
 * chip-select (separate from any display sharing the same bus).
 * @param int_pin Optional interrupt GPIO handle. Pass `NULL` to always poll.
 * @param config Optional pointer to initialization options. Pass `NULL` to use
 * default values.
 * @return STMPE610 handle, or `NULL` on failure (including a chip ID
 * mismatch - nothing responding correctly at the given chip-select).
 */
dev_stmpe610_t *dev_stmpe610_init(hw_deviceio_t *device, hw_gpio_t *int_pin,
                                  const dev_stmpe610_config_t *config);

/**
 * @brief Deinitialize an STMPE610 controller.
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 *
 * Passing `NULL` is safe and is a no-op.
 */
void dev_stmpe610_deinit(dev_stmpe610_t *stmpe610);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Report whether the STMPE610 interrupt line is currently asserted.
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 * @retval true The controller is signalling a pending update.
 * @retval false No pending update is signalled, or no interrupt pin exists.
 */
bool dev_stmpe610_irq_active(const dev_stmpe610_t *stmpe610);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Poll the controller and parse the latest touch sample.
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 * @param touch Receives the latest touch contact.
 * @retval true Read succeeded.
 * @retval false Read failed.
 *
 * When an interrupt pin is configured, this function skips the SPI
 * transaction when no touch is pending and the previous sample was already
 * idle.
 */
bool dev_stmpe610_poll(dev_stmpe610_t *stmpe610, dev_stmpe610_touch_t *touch);

/** @} */

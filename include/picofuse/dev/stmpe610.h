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
 * @brief Touch contact state reported to a dev_stmpe610_callback_t.
 * @ingroup STMPE610
 */
typedef enum {
  dev_stmpe610_touch_up = 0,   ///< Contact lifted - no longer touching.
  dev_stmpe610_touch_down = 1, ///< A new contact.
  dev_stmpe610_touch_move = 2, ///< An existing contact moved.
} dev_stmpe610_touch_event_t;

/**
 * @brief The single touch contact reported to a dev_stmpe610_callback_t -
 * unlike a capacitive controller, STMPE610 is resistive and only ever
 * reports one point at a time.
 * @ingroup STMPE610
 *
 * `x`/`y`/`z` are the controller's raw, uncalibrated 12-bit ADC readings
 * (roughly 0-4095) - not screen pixel coordinates. Each is a voltage-ratio
 * measurement across the resistive panel along one axis (and, for `z`, a
 * similar pressure/contact-resistance reading), so the exact range and
 * scale is specific to the physical panel and its wiring. Turning these
 * into pixel coordinates needs an application-level calibration step -
 * typically touching each screen corner once to record the raw min/max x
 * and y seen there, then linearly mapping subsequent raw readings into
 * the display's actual pixel range. This driver does not do that mapping
 * itself.
 */
typedef struct {
  dev_stmpe610_touch_event_t event; ///< Contact state for this sample.
  uint16_t x;                       ///< Raw X ADC reading - see struct doc.
  uint16_t y;                       ///< Raw Y ADC reading - see struct doc.
  uint8_t z;                        ///< Raw pressure ADC reading - see struct doc.
} dev_stmpe610_touch_t;

/**
 * @brief Touch event callback invoked from dev_stmpe610_poll().
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 * @param touch The touch contact that changed.
 * @param userdata User-defined data pointer passed to dev_stmpe610_init().
 */
typedef void (*dev_stmpe610_callback_t)(dev_stmpe610_t *stmpe610,
                                        const dev_stmpe610_touch_t *touch,
                                        void *userdata);

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
 * @param callback Optional callback invoked from dev_stmpe610_poll() each
 * time the touch state actually changes. Pass `NULL` if only
 * dev_stmpe610_irq_active() or the interrupt pin's own level matters.
 * @param userdata User-defined data pointer passed to `callback`.
 * @param config Optional pointer to initialization options. Pass `NULL` to use
 * default values.
 * @return STMPE610 handle, or `NULL` on failure (including a chip ID
 * mismatch - nothing responding correctly at the given chip-select).
 */
dev_stmpe610_t *dev_stmpe610_init(hw_deviceio_t *device, hw_gpio_t *int_pin,
                                  dev_stmpe610_callback_t callback,
                                  void *userdata,
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
 * @brief Poll the controller, invoking the callback if the touch state
 * changed.
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 *
 * When an interrupt pin is configured, this function skips the SPI
 * transaction entirely when no touch is pending and the previous sample was
 * already idle. The callback registered with dev_stmpe610_init() (if any)
 * is invoked only when a new contact, a moved contact, or a lifted contact
 * is actually detected - never for an unchanged, still-idle, or
 * still-touching-with-nothing-new poll.
 */
void dev_stmpe610_poll(dev_stmpe610_t *stmpe610);

/** @} */

/**
 * @file stmpe610.h
 * @brief STMicroelectronics STMPE610 resistive touch controller interface.
 * @defgroup STMPE610 STMPE610
 * @ingroup Touch
 *
 * This module provides a device-level API for STMPE610 resistive touch
 * controllers over SPI (e.g. Adafruit's 2.8" PiTFT resistive touch
 * display, which pairs it with an ILI9341 display controller on the same
 * SPI bus but a separate chip-select).
 */
#pragma once

#include <picofuse/hid/event.h>
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
  hw_gpio_t *int_pin;  ///< Optional interrupt GPIO handle. NULL to always
                       ///< poll instead - see dev_stmpe610_init()'s own doc.
} dev_stmpe610_config_t;

/**
 * @brief Touch event callback invoked from dev_stmpe610_poll().
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 * @param touch The touch contact that changed - unlike a capacitive
 * controller, STMPE610 is resistive and only ever reports one point at a
 * time, always as @ref hid_touch_t::slot `0`. @ref hid_touch_t::state is
 * @ref hid_state_on for a new contact, @ref hid_state_repeat for an
 * existing one that's still down but moved, and @ref hid_state_off for a
 * lifted one - see dev_ft6236_poll()'s own doc on the "repeat always
 * follows on" guarantee this driver makes the same way. @ref
 * hid_touch_t::point and @ref hid_touch_t::pressure are the controller's
 * raw, uncalibrated 12-bit ADC readings (roughly 0-4095 for `point`, 8
 * bits for `pressure`) - not screen pixel coordinates or a real
 * pressure unit. Each is a voltage-ratio measurement across the
 * resistive panel along one axis (and, for `pressure`, a similar
 * contact-resistance reading), so the exact range and scale is specific
 * to the physical panel and its wiring. Turning `point` into pixel
 * coordinates needs an application-level calibration step - typically
 * touching each screen corner once to record the raw min/max seen
 * there, then linearly mapping subsequent raw readings into the
 * display's actual pixel range. This driver does not do that mapping
 * itself.
 * @param userdata User-defined data pointer passed to
 * dev_stmpe610_set_callback().
 */
typedef void (*dev_stmpe610_callback_t)(dev_stmpe610_t *stmpe610,
                                        const hid_touch_t *touch,
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
 * @param config Optional pointer to initialization options - see @ref
 * dev_stmpe610_config_t's own doc for the interrupt pin. Pass `NULL` to
 * use default values (no interrupt pin - always poll).
 * @return STMPE610 handle, or `NULL` on failure (including a chip ID
 * mismatch - nothing responding correctly at the given chip-select).
 *
 * No callback is attached here - use @ref dev_stmpe610_set_callback
 * afterward if @ref dev_stmpe610_poll should report touches somewhere.
 */
dev_stmpe610_t *dev_stmpe610_init(hw_deviceio_t *device,
                                  const dev_stmpe610_config_t *config);

/**
 * @brief Deinitialize an STMPE610 controller.
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 *
 * Passing `NULL` is safe and is a no-op.
 */
void dev_stmpe610_deinit(dev_stmpe610_t *stmpe610);

/**
 * @brief Set (or replace) the callback invoked from dev_stmpe610_poll().
 * @ingroup STMPE610
 * @param stmpe610 STMPE610 handle.
 * @param callback New callback, or `NULL` to stop invoking one.
 * @param userdata User-defined data pointer passed to `callback`.
 *
 * Replaces whatever callback a previous call to this set, if any - there
 * is no way to have more than one observer. dev_stmpe610_register_hid()
 * uses this to redirect touch events into HID, taking over from whatever
 * the caller may have already set - see its own doc.
 */
void dev_stmpe610_set_callback(dev_stmpe610_t *stmpe610,
                               dev_stmpe610_callback_t callback,
                               void *userdata);

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
 * already idle. The callback set with dev_stmpe610_set_callback() (if any)
 * is invoked only when a new contact, a moved contact, or a lifted contact
 * is actually detected - never for an unchanged, still-idle, or
 * still-touching-with-nothing-new poll. hid_state_repeat always follows an
 * hid_state_on for the same slot (0, the only one this controller ever
 * uses) - unlike dev_ft6236_poll(), which needs to synthesize that
 * guarantee for a torn-read edge case (see its own doc), it holds here
 * naturally: this driver's own `had_touch` tracking already decides
 * on-vs-repeat the same way it decides when to stop reporting at all, so
 * there's no path that reaches "repeat" without "on" already delivered.
 */
void dev_stmpe610_poll(dev_stmpe610_t *stmpe610);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// HID

/** @name HID
 * @{ */

/**
 * @brief Register an STMPE610 controller as a HID touchscreen source.
 * @ingroup STMPE610
 * @param instance HID instance that owns the registration.
 * @param stmpe610 Already-initialized STMPE610 handle - HID observes it,
 * it does not take ownership (dev_stmpe610_deinit() remains the caller's
 * own responsibility, same as dev_stmpe610_init()'s own device/int_pin
 * arguments).
 * @param polling_interval_ms How often HID calls dev_stmpe610_poll() for
 * this device, in milliseconds. Clamped up to a small minimum
 * (`STMPE610_HID_MIN_POLLING_INTERVAL_MS`) purely to avoid needless SPI
 * chatter - unlike FT6236 (see its own @ref dev_ft6236_register_hid),
 * this isn't a correctness requirement: the FIFO_STA/FIFO data registers
 * this driver reads are hardware-arbitrated, so polling faster than the
 * panel's own scan rate just finds FIFO_STA empty more often, not torn
 * data.
 * @param userdata Opaque user data retrievable via hid_device_userdata()
 * on the returned device. To reach @p stmpe610 itself instead, use
 * hid_device_handle().
 * @return Registered HID device descriptor, or `NULL` on failure.
 *
 * Emits a hid_event_type_touch event (via hid_event_queue_touch()) for
 * every touch dev_stmpe610_poll() reports as changed - see
 * dev_stmpe610_callback_t's own doc for exactly what state/point/slot/
 * pressure mean here.
 *
 * Replaces whatever callback @p stmpe610 already had attached via
 * dev_stmpe610_set_callback(), the same way hid_register_wifi() replaces
 * whatever callback its own hw_wifi_t already had - avoiding that
 * collision is the caller's own responsibility, same as there.
 *
 * hid_deregister() detaches the callback (equivalent to
 * `dev_stmpe610_set_callback(stmpe610, NULL, NULL)`) but leaves @p
 * stmpe610 itself initialized.
 */
hid_device_t *dev_stmpe610_register_hid(hid_t *instance,
                                        dev_stmpe610_t *stmpe610,
                                        uint32_t polling_interval_ms,
                                        void *userdata);

/** @} */

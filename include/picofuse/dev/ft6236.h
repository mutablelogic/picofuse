/**
 * @file ft6236.h
 * @brief FocalTech FT6236 capacitive touch controller interface.
 * @defgroup FT6236 FT6236
 * @ingroup Touch
 *
 * This module provides a device-level API for FT6236-compatible capacitive
 * touch controllers over I2C.
 */
#pragma once

#include <picofuse/hid/event.h>
#include <picofuse/hw.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque FT6236 handle.
 * @ingroup FT6236
 */
typedef struct dev_ft6236_t dev_ft6236_t;

/**
 * @def DEV_FT6236_I2C_ADDR_DEFAULT
 * @ingroup FT6236
 * @brief Fixed 7-bit I2C address
 */
#define DEV_FT6236_I2C_ADDR_DEFAULT 0x38u

/**
 * @brief Maximum number of simultaneous touch contacts reported by FT6236.
 * @ingroup FT6236
 */
#define DEV_FT6236_MAX_POINTS 2u

/**
 * @brief Optional FT6236 initialization options.
 * @ingroup FT6236
 */
typedef struct {
  bool irq_active_low;  ///< True when the interrupt pin is active low.
  hw_gpio_t *int_pin;   ///< Optional interrupt GPIO handle. NULL to always
                        ///< poll instead - see dev_ft6236_init()'s own doc.
  hw_gpio_t *reset_pin; ///< Optional hardware reset GPIO handle. NULL to
                        ///< skip the reset pulse and assume the controller
                        ///< is already up - see dev_ft6236_init()'s own doc.
} dev_ft6236_config_t;

/**
 * @brief Touch event callback invoked from dev_ft6236_poll().
 * @ingroup FT6236
 * @param ft6236 FT6236 handle.
 * @param touch The touch contact that changed - @ref hid_touch_t::state
 * is @ref hid_state_on for a new contact, @ref hid_state_repeat for an
 * existing one that's still down but moved, and @ref hid_state_off for a
 * lifted one; @ref hid_touch_t::point is in panel pixels; @ref
 * hid_touch_t::slot is a stable index in `[0, DEV_FT6236_MAX_POINTS)`,
 * not FT6236's own 4-bit hardware track ID (which has no such range
 * guarantee) - a lift always reports the same slot its own down/move
 * events did, for exactly this reason.
 * @param userdata User-defined data pointer passed to
 * dev_ft6236_set_callback().
 */
typedef void (*dev_ft6236_callback_t)(dev_ft6236_t *ft6236,
                                      const hid_touch_t *touch, void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Fill an FT6236 config struct with safe defaults.
 * @ingroup FT6236
 * @param config Config structure to initialize.
 */
void dev_ft6236_default_config(dev_ft6236_config_t *config);

/**
 * @brief Initialize an FT6236-compatible touch controller over I2C.
 * @ingroup FT6236
 * @param device I2C device handle from hw_i2c_init() / hw_i2c_init_default()
 * / hw_i2c_init_device(), already opened at the controller's address (see
 * @ref DEV_FT6236_I2C_ADDR_DEFAULT - some boards wire an FT6236-compatible
 * variant at a different fixed address instead, check the board's own
 * pinout before assuming the default).
 * @param config Optional pointer to initialization options - see @ref
 * dev_ft6236_config_t's own doc for the interrupt/reset pins. Pass `NULL`
 * to use default values (no interrupt pin, no reset pin).
 * @return FT6236 handle, or `NULL` on failure (including nothing
 * responding correctly at the device's own address).
 *
 * If @ref dev_ft6236_config_t::reset_pin is set, this pulses it low (the
 * FT6X36 datasheet's own reset line is active-low, with an internal
 * pull-up - see its Figure 3-2/3-10) for the datasheet's minimum Trst
 * (5ms), then waits the datasheet's minimum Trsi (300ms) before the
 * controller is expected to report valid data - both real, measured
 * requirements (FT6X36 datasheet Table 3-5 "Power on/Reset/Wake Sequence
 * Parameters"), not arbitrary values. Without a reset pin, the controller
 * is assumed to already be up (e.g. reset by its own power-on sequence
 * before this is called).
 *
 * No callback is attached here - use @ref dev_ft6236_set_callback
 * afterward if @ref dev_ft6236_poll should report touches somewhere.
 */
dev_ft6236_t *dev_ft6236_init(hw_deviceio_t *device,
                              const dev_ft6236_config_t *config);

/**
 * @brief Deinitialize an FT6236 controller.
 * @ingroup FT6236
 * @param ft6236 FT6236 handle.
 *
 * Passing `NULL` is safe and is a no-op.
 */
void dev_ft6236_deinit(dev_ft6236_t *ft6236);

/**
 * @brief Set (or replace) the callback invoked from dev_ft6236_poll().
 * @ingroup FT6236
 * @param ft6236 FT6236 handle.
 * @param callback New callback, or `NULL` to stop invoking one.
 * @param userdata User-defined data pointer passed to `callback`.
 *
 * Replaces whatever callback a previous call to this set, if any -
 * there is no way to have more than one observer.
 * dev_ft6236_register_hid() uses this to redirect touch events into HID,
 * taking over from whatever the caller may have already set - see its
 * own doc.
 */
void dev_ft6236_set_callback(dev_ft6236_t *ft6236,
                             dev_ft6236_callback_t callback, void *userdata);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Report whether the FT6236 interrupt line is currently asserted.
 * @ingroup FT6236
 * @param ft6236 FT6236 handle.
 * @retval true The controller is signalling a pending update.
 * @retval false No pending update is signalled, or no interrupt pin exists.
 */
bool dev_ft6236_irq_active(const dev_ft6236_t *ft6236);

/**
 * @brief Report whether an interrupt pin was configured for this handle.
 * @ingroup FT6236
 * @param ft6236 FT6236 handle.
 * @retval true @ref dev_ft6236_config_t::int_pin was non-NULL at
 * dev_ft6236_init() time - dev_ft6236_poll() can cheaply skip its I2C
 * transaction when idle (see its own doc).
 * @retval false No interrupt pin - every dev_ft6236_poll() call does a
 * real I2C transaction.
 *
 * Unlike dev_ft6236_irq_active(), which is ambiguous for exactly this
 * question (`false` there means either "no pin" or "pin idle"), this
 * only answers whether a pin exists at all.
 */
bool dev_ft6236_has_interrupt_pin(const dev_ft6236_t *ft6236);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Poll the controller, invoking the callback for each touch slot
 * whose state actually changed.
 * @ingroup FT6236
 * @param ft6236 FT6236 handle.
 *
 * When an interrupt pin is configured, this function skips the I2C
 * transaction when no touch is pending and the previous frame was already
 * idle - except at least once every `FT6236_IRQ_RECONCILE_MS`, regardless
 * of the interrupt pin's own level: INT is a per-frame data-ready pulse,
 * not a level held for the duration of a touch (see @ref
 * dev_ft6236_register_hid's own doc), so a poll that happens to land
 * between two pulses would otherwise never notice a touch that started
 * and, without this, could go undetected indefinitely rather than just
 * briefly delayed. The callback set with dev_ft6236_set_callback() (if any) is
 * invoked once per touch slot (up to @ref DEV_FT6236_MAX_POINTS) whose
 * contact state changed since the last poll - a new contact, a moved
 * contact (different X/Y from last time), or a lifted contact - never for
 * an unchanged, still-idle, or still-touching-with-nothing-new slot. Unlike
 * a FIFO-backed controller (see dev_stmpe610_poll()'s own doc), FT6236's
 * registers always report the current frame regardless of whether
 * anything actually changed since the last read, so this compares each
 * slot against its own previous state itself rather than relying on the
 * controller to say so.
 *
 * A slot's callback sequence always starts with @ref hid_state_on before
 * any @ref hid_state_repeat for it - if the last state actually
 * delivered for a slot was @ref hid_state_off and the newly-parsed state
 * is @ref hid_state_repeat (the down transition itself was missed, or
 * this is the first poll to see it since a prior off), a synthetic
 * @ref hid_state_on for the same contact is delivered first. Consumers
 * can rely on "repeat" always meaning "this slot is already known to be
 * down", never a state they have to infer for themselves.
 */
void dev_ft6236_poll(dev_ft6236_t *ft6236);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// HID

/** @name HID Integration
 * @{ */

/**
 * @brief Register an FT6236 controller as a HID touchscreen source.
 * @ingroup FT6236
 * @param instance HID instance that owns the registration.
 * @param ft6236 Already-initialized FT6236 handle - HID observes it, it
 * does not take ownership (dev_ft6236_deinit() remains the caller's own
 * responsibility, same as dev_ft6236_init()'s own device/int_pin/
 * reset_pin arguments).
 * @param polling_interval_ms How often HID calls dev_ft6236_poll() for
 * this device, in milliseconds. Always clamped up to a real minimum
 * (see `FT6236_HID_MIN_POLLING_INTERVAL_MS`'s own doc), regardless of
 * whether @p ft6236 has an interrupt pin - the FT6X36's own INT line is
 * a per-frame "data ready" pulse, not a level held for the duration of a
 * touch, so polling faster than its own scan cadence risks reading its
 * register burst mid-update rather than getting anything cheaper or more
 * responsive. An interrupt pin still makes each individual poll cheaper
 * (dev_ft6236_poll() skips the I2C transaction whenever nothing is
 * pending), just not faster than this floor.
 * @param userdata Opaque user data retrievable via hid_device_userdata()
 * on the returned device. To reach @p ft6236 itself instead, use
 * hid_device_handle().
 * @return Registered HID device descriptor, or `NULL` on failure.
 *
 * Emits a hid_event_type_touch event (via hid_event_queue_touch()) for
 * every touch dev_ft6236_poll() reports as changed - hid_state_on for a
 * new contact, hid_state_repeat for one that's still down but moved, and
 * hid_state_off for a lift - see dev_ft6236_callback_t's own doc for what
 * the event's `slot` means (a stable per-contact index, not FT6236's own
 * hardware track ID).
 *
 * There is one poll-driven code path here regardless of whether @p
 * ft6236 was given an interrupt pin at dev_ft6236_init() time - not two,
 * and not even a different effective polling interval, per @p
 * polling_interval_ms's own doc above. A genuinely interrupt-*driven*
 * path (the controller's own INT line triggering a real GPIO edge
 * callback via hw_gpio_set_callback(), rather than HID's own timer
 * calling in on a schedule) isn't implemented - hw_gpio_set_callback()
 * is a single global slot shared system-wide, already claimed by
 * hid/gpio.c's own dispatcher the moment any GPIO-backed HID device
 * (e.g. a button, via hid_register_gpio_input()) is registered, and
 * calling it again here directly would silently steal that slot from -
 * or lose it to - whichever registers second.
 *
 * Replaces whatever callback @p ft6236 already had attached via
 * dev_ft6236_set_callback(), the same way hid_register_wifi() replaces
 * whatever callback its own hw_wifi_t already had - avoiding that
 * collision is the caller's own responsibility, same as there.
 *
 * hid_deregister() detaches the callback (equivalent to
 * `dev_ft6236_set_callback(ft6236, NULL, NULL)`) but leaves @p ft6236
 * itself initialized.
 */
hid_device_t *dev_ft6236_register_hid(hid_t *instance, dev_ft6236_t *ft6236,
                                      uint32_t polling_interval_ms,
                                      void *userdata);

/** @} */

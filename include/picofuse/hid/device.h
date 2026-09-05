/**
 * @file device.h
 * @brief HID device lifecycle and polling interface.
 * @ingroup HID
 */
#pragma once
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Maximum number of HID devices tracked by one HID instance.
 * @ingroup HID
 *
 * Override at compile time, for example: `-DHID_DEVICE_CAPACITY=16`.
 */
#ifndef HID_DEVICE_CAPACITY
#define HID_DEVICE_CAPACITY 32u
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque HID instance handle.
 * @ingroup HID
 */
typedef struct hid_t hid_t;

/**
 * @brief HID child-device descriptor tracked by a HID instance.
 * @ingroup HID
 */
typedef struct hid_device_t hid_device_t;

/**
 * @brief HID device type classification.
 * @ingroup HID
 */
typedef enum {
  hid_type_none = 0,
  hid_type_gpio = 1,
  hid_type_timer = 2,
  hid_type_signal = 3,
  hid_type_evdev = 4,
  hid_type_usb = 5,
  hid_type_wifi = 6,
  hid_type_bluetooth = 7,
  hid_type_infrared = 8,
  hid_type_other = 9,
} hid_type_t;

/**
 * @brief Callback operation table for HID device backends.
 * @ingroup HID
 *
 * Each callback returns true on success and false on failure.
 */
typedef struct {
  bool (*init)(hid_device_t *device, void *userdata);
  bool (*read)(hid_device_t *device, void *userdata);
  bool (*deinit)(hid_device_t *device, void *userdata);
} hid_device_callbacks_t;

/**
 * @brief Coarse semantic classification of a HID device.
 * @ingroup HID
 *
 * Unlike hid_type_t (which identifies the backend mechanism, e.g. gpio vs
 * evdev vs timer), this describes what kind of thing the device is to an
 * application. For evdev devices this is determined heuristically from the
 * event/key/axis capability bits the kernel reports for the device node;
 * see hid_evdev_list().
 */
typedef enum {
  hid_class_unknown = 0,
  hid_class_keyboard = 1,
  hid_class_mouse = 2,
  hid_class_joystick = 3,
  hid_class_touchscreen = 4,
  hid_class_sensor = 5,
} hid_class_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a HID device instance.
 * @ingroup HID
 * @param queue Event queue used by this HID instance.
 * @return HID instance, or NULL on failure.
 */
hid_t *hid_init(sys_event_queue_t *queue);

/**
 * @brief Deinitialize a HID device instance.
 * @ingroup HID
 * @param instance HID instance.
 */
void hid_deinit(hid_t *instance);

/**
 * @brief Poll a HID device for pending input.
 * @ingroup HID
 * @param instance HID instance.
 * @retval true Input was processed.
 * @retval false No input was available or the device is invalid.
 */
bool hid_poll(hid_t *instance);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Register a generic HID device using callback operations.
 * @ingroup HID
 * @param instance HID instance that owns the registration.
 * @param name Device name.
 * @param id Device identifier.
 * @param type Device type (backend mechanism) classification.
 * @param hid_class Device semantic classification (see hid_class_t). Pass
 * hid_class_unknown when none applies.
 * @param polling_interval_ms Polling interval in milliseconds for read
 * callbacks. Use 0 to evaluate on every hid_poll() call.
 * @param userdata Opaque user data passed to callback functions.
 * @param callbacks Callback operation table.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register(hid_t *instance, const char *name, uint32_t id,
                           hid_type_t type, hid_class_t hid_class,
                           uint32_t polling_interval_ms, void *userdata,
                           hid_device_callbacks_t callbacks);

/**
 * @brief Register a GPIO pin as HID input.
 * @ingroup HID
 * @param instance HID instance that owns the GPIO registration.
 * @param bank GPIO bank index.
 * @param pin GPIO pin index.
 * @param keycode HID keycode reported for this input.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_gpio_input(hid_t *instance, uint8_t bank,
                                      uint8_t pin, uint16_t keycode);

/**
 * @brief Register a GPIO pin as HID input with pull-up.
 * @ingroup HID
 * @param instance HID instance that owns the GPIO registration.
 * @param bank GPIO bank index.
 * @param pin GPIO pin index.
 * @param keycode HID keycode reported for this input.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_gpio_pullup(hid_t *instance, uint8_t bank,
                                       uint8_t pin, uint16_t keycode);

/**
 * @brief Register a GPIO pin as HID input with pull-down.
 * @ingroup HID
 * @param instance HID instance that owns the GPIO registration.
 * @param bank GPIO bank index.
 * @param pin GPIO pin index.
 * @param keycode HID keycode reported for this input.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_gpio_pulldown(hid_t *instance, uint8_t bank,
                                         uint8_t pin, uint16_t keycode);

/**
 * @brief Register an ADC channel as a polling HID metric source.
 * @ingroup HID
 * @param instance HID instance that owns the registration.
 * @param channel ADC channel number (see `hw_adc_gpio_pin()`/
 * `hw_adc_gpio_channel()`). Must be backed by a GPIO pin on the current
 * platform; use @ref hid_register_temperature for the internal
 * temperature-sensor channel instead.
 * @param metric_name Name reported on the published metric event. Must
 * remain valid for the lifetime of the registration (a string literal is
 * fine); NULL defaults to `"raw_16"`.
 * @param num_samples Number of ADC conversions to average per read (see
 * `hw_adc_read_16()`). 0 or 1 takes a single, immediate reading.
 * @param polling_interval_ms Polling interval in milliseconds.
 * @details Passing 0 uses a default interval of 5000 ms.
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if the channel has no GPIO pin).
 *
 * Resolves the channel to its GPIO pin (`hw_adc_gpio_pin()`), reads it on
 * every poll, and publishes a `hid_event_type_metric` event named
 * @p metric_name (0-65535) whenever the value has changed since the last
 * poll, mirroring the change-detection behavior of
 * @ref dev_bme680_hid_register_i2c. No temperature metric is reported here;
 * see @ref hid_register_temperature for the internal temperature-sensor
 * channel.
 */
hid_device_t *hid_register_adc(hid_t *instance, uint8_t channel,
                               const char *metric_name, uint16_t num_samples,
                               uint32_t polling_interval_ms);

/**
 * @brief Register the internal temperature-sensor channel as a polling HID
 * metric source.
 * @ingroup HID
 * @param instance HID instance that owns the registration.
 * @param polling_interval_ms Polling interval in milliseconds.
 * @details Passing 0 uses a default interval of 5000 ms.
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if the platform has no internal temperature sensor).
 *
 * Reads the internal temperature-sensor ADC channel (see
 * `hw_adc_init_temperature()`), averaged over a fixed number of samples
 * internal to this module, on every poll and publishes a
 * `hid_event_type_metric` `"temp"` (degrees Celsius) event whenever the
 * value has changed since the last poll. See @ref hid_register_adc for a
 * GPIO-pin ADC source with a caller-controlled sample count.
 */
hid_device_t *hid_register_temperature(hid_t *instance,
                                       uint32_t polling_interval_ms);

/**
 * @brief Register a user button as a HID input source.
 * @ingroup HID
 * @param instance HID instance that owns the user-button registration.
 * @param keycode HID keycode reported for this input.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_user_button(hid_t *instance, uint16_t keycode);

/**
 * @brief Register a timer-backed HID source.
 * @ingroup HID
 * @param instance HID instance that owns the timer registration.
 * @param id Device identifier.
 * @param interval_ms Timer period in milliseconds.
 * @param repeating True for periodic timers, false for one-shot timers.
 * @param userdata Opaque user data stored with the timer.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_timer(hid_t *instance, uint32_t id,
                                 uint32_t interval_ms, bool repeating,
                                 void *userdata);

/**
 * @brief Register an environment-signal HID source.
 * @ingroup HID
 * @param instance HID instance that owns the signal registration.
 * @return Registered HID device descriptor, or NULL on failure.
 *
 * The signal source captures environment signals and emits
 * `hid_event_type_signal` events when those signals are observed.
 */
hid_device_t *hid_register_signal(hid_t *instance);

/**
 * @brief Register a USB host hotplug observer.
 * @ingroup HID
 * @param instance HID instance that owns the USB registration.
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if the platform has no USB host controller support built in).
 *
 * Initializes the USB host subsystem (see `hw_usb_init()`), which
 * enumerates already-attached devices through the same attach/detach
 * callback used for live hotplug. Only one USB registration is permitted
 * at a time, since `hw_usb_init()` is itself a process-wide singleton.
 *
 * Attach/detach activity is currently only logged via `sys_debugf()`; it
 * is not yet delivered as HID events.
 */
hid_device_t *hid_register_usb(hid_t *instance);

/**
 * @brief Register a Wi-Fi connection-state observer.
 * @ingroup HID
 * @param instance HID instance that owns the Wi-Fi registration.
 * @param country_code Country code for the Wi-Fi region (e.g., "US", "EU").
 * If NULL, defaults to "XX" (worldwide). See `hw_wifi_init_client()`.
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if the platform has no Wi-Fi hardware support built in).
 *
 * Initializes the Wi-Fi client subsystem (see `hw_wifi_init_client()`) and
 * emits `hid_event_type_wifi` events for every status change it reports
 * (joining, connected, disconnected, scan results, errors — see
 * hw_wifi_event_t). Only one Wi-Fi registration is permitted at a time,
 * since `hw_wifi_init_client()` is itself a process-wide singleton.
 *
 * This registers an observer only; it does not expose scan/connect/
 * disconnect actions. To drive the connection, retrieve the underlying
 * `hw_wifi_t*` handle via `hid_device_userdata()` on the returned device
 * and call `hw_wifi_scan()`/`hw_wifi_connect()`/`hw_wifi_disconnect()`
 * directly (mirroring how `hid_type_gpio` devices expose their backing
 * `hw_gpio_t*` the same way).
 */
hid_device_t *hid_register_wifi(hid_t *instance, const char *country_code);

/**
 * @brief Deregister and remove a HID device.
 * @ingroup HID
 * @param instance HID instance that owns the device.
 * @param device HID device handle.
 * @retval true Device was removed.
 * @retval false Instance or device handle was invalid.
 */
bool hid_deregister(hid_t *instance, hid_device_t *device);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Enumerate registered HID devices.
 * @ingroup HID
 * @param device Current device pointer, or NULL to get the first device.
 * @return Next device pointer, or NULL when no more devices are available.
 */
hid_device_t *hid_device_next(hid_device_t *device);

/**
 * @brief Get metadata for a registered HID device.
 * @ingroup HID
 * @param device HID device handle.
 * @param out_name Receives device name when non-NULL.
 * @param out_id Receives device id when non-NULL.
 * @param out_type Receives device type when non-NULL.
 * @param out_class Receives device classification when non-NULL. Devices
 * registered with hid_class_unknown (the default choice when no more
 * specific classification applies) report hid_class_unknown here.
 * @retval true Metadata was returned.
 * @retval false Device handle was invalid.
 */
bool hid_device_info(const hid_device_t *device, const char **out_name,
                     uint32_t *out_id, hid_type_t *out_type,
                     hid_class_t *out_class);

/**
 * @brief Get the userdata pointer associated with a registered HID device.
 * @ingroup HID
 * @param device HID device handle.
 * @return Device userdata pointer, or NULL when the handle is invalid.
 */
void *hid_device_userdata(const hid_device_t *device);

/** @} */

/**
 * @file device.h
 * @brief HID device lifecycle and polling interface.
 * @ingroup HID
 */
#pragma once
#include <picofuse/hw/wifi.h>
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

/**
 * @brief Size in bytes of the per-device scratch context space embedded in
 * every hid_device_t.
 * @ingroup HID
 *
 * Override at compile time, for example: `-DHID_DEVICE_CONTEXT_SIZE=64`.
 */
#ifndef HID_DEVICE_CONTEXT_SIZE
#define HID_DEVICE_CONTEXT_SIZE 32u
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
  hid_type_iostream = 10,
} hid_type_t;

/**
 * @brief Callback operation table for HID device backends.
 * @ingroup HID
 *
 * Each callback returns true on success and false on failure.
 */
typedef struct {
  bool (*init)(hid_device_t *device, void *userdata); ///< Called once, from
                                                       ///< hid_register().
  bool (*read)(hid_device_t *device, void *userdata); ///< Called from
                                                       ///< hid_poll(), per
                                                       ///< polling_interval_ms.
  bool (*deinit)(hid_device_t *device, void *userdata); ///< Called once,
                                                        ///< from
                                                        ///< hid_deregister().
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
 * @param userdata Opaque user data retrievable via hid_device_userdata() on
 * the returned device. To reach the backing hw_gpio_t* handle instead
 * (e.g. to call hw_gpio_set_mode() directly), use hid_device_handle().
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_gpio_input(hid_t *instance, uint8_t bank,
                                      uint8_t pin, uint16_t keycode,
                                      void *userdata);

/**
 * @brief Register a GPIO pin as HID input with pull-up.
 * @ingroup HID
 * @param instance HID instance that owns the GPIO registration.
 * @param bank GPIO bank index.
 * @param pin GPIO pin index.
 * @param keycode HID keycode reported for this input.
 * @param userdata Opaque user data retrievable via hid_device_userdata() -
 * see hid_register_gpio_input()'s own doc.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_gpio_pullup(hid_t *instance, uint8_t bank,
                                       uint8_t pin, uint16_t keycode,
                                       void *userdata);

/**
 * @brief Register a GPIO pin as HID input with pull-down.
 * @ingroup HID
 * @param instance HID instance that owns the GPIO registration.
 * @param bank GPIO bank index.
 * @param pin GPIO pin index.
 * @param keycode HID keycode reported for this input.
 * @param userdata Opaque user data retrievable via hid_device_userdata() -
 * see hid_register_gpio_input()'s own doc.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_gpio_pulldown(hid_t *instance, uint8_t bank,
                                         uint8_t pin, uint16_t keycode,
                                         void *userdata);

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
 * @param userdata Opaque user data retrievable via hid_device_userdata() on
 * the returned device. To reach the backing hw_adc_t* handle instead, use
 * hid_device_handle().
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if the channel has no GPIO pin).
 *
 * Resolves the channel to its GPIO pin (`hw_adc_gpio_pin()`), reads it on
 * every poll, and publishes a `hid_event_type_metric` event named
 * @p metric_name (0-65535) whenever the value has changed since the last
 * poll, the same change-detection behavior @ref hid_register_temperature
 * itself uses for the internal temperature-sensor channel. No temperature
 * metric is reported here; see @ref hid_register_temperature for that.
 */
hid_device_t *hid_register_adc(hid_t *instance, uint8_t channel,
                               const char *metric_name, uint16_t num_samples,
                               uint32_t polling_interval_ms, void *userdata);

/**
 * @brief Register the internal temperature-sensor channel as a polling HID
 * metric source.
 * @ingroup HID
 * @param instance HID instance that owns the registration.
 * @param polling_interval_ms Polling interval in milliseconds.
 * @details Passing 0 uses a default interval of 5000 ms.
 * @param userdata Opaque user data retrievable via hid_device_userdata() -
 * see hid_register_adc()'s own doc.
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
                                       uint32_t polling_interval_ms,
                                       void *userdata);

/**
 * @brief Register a user button as a HID input source.
 * @ingroup HID
 * @param instance HID instance that owns the user-button registration.
 * @param keycode HID keycode reported for this input.
 * @param userdata Opaque user data retrievable via hid_device_userdata() -
 * see hid_register_gpio_input()'s own doc.
 * @return Registered HID device descriptor, or NULL on failure.
 */
hid_device_t *hid_register_user_button(hid_t *instance, uint16_t keycode,
                                       void *userdata);

/**
 * @brief Register a timer-backed HID source.
 * @ingroup HID
 * @param instance HID instance that owns the timer registration.
 * @param id Device identifier.
 * @param interval_ms Timer period in milliseconds.
 * @param repeating True for periodic timers, false for one-shot timers.
 * @param userdata Opaque user data retrievable via hid_device_userdata()
 * and forwarded as hid_timer_t.userdata on every event this timer fires.
 * To reach the backing sys_timer_t* handle instead, use
 * hid_device_handle().
 * @return Registered HID device descriptor, or NULL on failure.
 *
 * For @p repeating == false, do not call hid_deregister() once the timer
 * fires - the device is automatically deregistered when its
 * `hid_event_type_timer` event is released with hid_event_free(); see
 * that function's own doc.
 */
hid_device_t *hid_register_timer(hid_t *instance, uint32_t id,
                                 uint32_t interval_ms, bool repeating,
                                 void *userdata);

/**
 * @brief Register an environment-signal HID source.
 * @ingroup HID
 * @param instance HID instance that owns the signal registration.
 * @param userdata Opaque user data retrievable via hid_device_userdata() on
 * the returned device.
 * @return Registered HID device descriptor, or NULL on failure.
 *
 * The signal source captures environment signals and emits
 * `hid_event_type_signal` events when those signals are observed.
 */
hid_device_t *hid_register_signal(hid_t *instance, void *userdata);

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
 *
 * @todo Not implemented yet - always returns NULL. There is no
 * `hw_usb_init()`/`hw_usb_t` backend anywhere in this codebase yet (see
 * `src/picofuse/hid/usb.c`); this needs a real USB host module under
 * `picofuse/hw` before this can do anything.
 */
hid_device_t *hid_register_usb(hid_t *instance);

/**
 * @brief Register a Wi-Fi connection-state observer.
 * @ingroup HID
 * @param instance HID instance that owns the Wi-Fi registration.
 * @param wifi Already-initialized Wi-Fi handle, from hw_wifi_init_client(),
 * hw_wifi_init_accesspoint(), or hw_wifi_init_device(). HID does not
 * create, own, or deinitialize this handle - only observes it - so
 * whichever part of the program brought the radio up is responsible for
 * eventually calling hw_wifi_deinit() on it.
 * @param userdata Opaque user data retrievable via hid_device_userdata() on
 * the returned device. To reach @p wifi itself instead, use
 * hid_device_handle().
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if @p wifi is NULL).
 *
 * Attaches a callback to @p wifi via hw_wifi_set_callback() and emits a
 * hid_event_type_wifi event for every status change it reports (joining,
 * connected, disconnected, scan results, errors — see hw_wifi_event_t).
 * hw_wifi_set_callback() replaces whatever callback @p wifi already had
 * attached, if any - this and any other code that also wants to observe
 * @p wifi directly will conflict with each other over that single slot.
 *
 * This registers an observer only; it does not expose scan/connect/
 * disconnect actions. To drive the connection, retrieve @p wifi via
 * `hid_device_handle()` on the returned device and call
 * `hw_wifi_scan()`/`hw_wifi_connect()`/`hw_wifi_disconnect()` directly
 * (mirroring how `hid_type_gpio` devices expose their backing
 * `hw_gpio_t*` the same way).
 *
 * hid_deregister() detaches the callback (equivalent to
 * `hw_wifi_set_callback(wifi, NULL, NULL)`) but leaves @p wifi itself
 * initialized.
 */
hid_device_t *hid_register_wifi(hid_t *instance, hw_wifi_t *wifi,
                                void *userdata);

/**
 * @brief Register a stream-readiness observer.
 * @ingroup HID
 * @param instance HID instance that owns the registration.
 * @param stream Already-open stream, from sys_string_read()/_open(),
 * sys_stdin/sys_stdout, or a hardware-backed stream such as
 * hw_uart_init()'s. HID does not create, own, or close this stream - only
 * observes it - so whichever part of the program opened it is responsible
 * for eventually calling sys_iostream_close() on it.
 * @param userdata Opaque user data retrievable via hid_device_userdata() on
 * the returned device. To reach @p stream itself instead, use
 * hid_device_handle().
 * @return Registered HID device descriptor, or NULL on failure (for
 * example, if @p stream is NULL or its backend doesn't support readiness
 * notifications - see sys_iostream_set_callback()).
 *
 * Attaches a callback to @p stream via sys_iostream_set_callback() and
 * emits a hid_event_type_iostream event whenever it becomes ready for
 * reading and/or writing (see sys_iostream_event_t) - most usefully, when
 * there is data available to read without blocking. Like
 * hid_register_wifi(), sys_iostream_set_callback() replaces whatever
 * callback @p stream already had attached, if any.
 *
 * This registers an observer only; it does not read or write @p stream
 * itself. Retrieve it via `hid_device_handle()` on the returned device and
 * call `sys_iostream_read()`/`_write()`/`_peek()` directly.
 *
 * hid_deregister() detaches the callback (equivalent to
 * `sys_iostream_set_callback(stream, NULL, NULL)`) but leaves @p stream
 * itself open.
 */
hid_device_t *hid_register_iostream(hid_t *instance, sys_iostream_t *stream,
                                    void *userdata);

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
 * @return Whatever @p userdata the device was registered with (see
 * hid_register() and the various hid_register_*() convenience functions),
 * or NULL when the handle is invalid or no userdata was supplied.
 *
 * This is always the caller's own opaque pointer, uniformly across every
 * registration function - it never aliases a backend's own hardware/system
 * handle (see hid_device_handle() for that).
 */
void *hid_device_userdata(const hid_device_t *device);

/**
 * @brief Get a device's backend-owned handle, if it has one.
 * @ingroup HID
 * @param device HID device handle.
 * @return The device's own backend handle - a `hw_gpio_t*` for
 * hid_type_gpio (including hid_register_user_button()), a `hw_wifi_t*`
 * for hid_type_wifi, a `sys_timer_t*` for hid_type_timer, a
 * `sys_iostream_t*` for hid_type_iostream, or a `hw_adc_t*` for an
 * ADC/temperature device (hid_type_other, hid_class_sensor) - or NULL if
 * the handle is invalid or this device type has no single such handle
 * (e.g. hid_type_signal, or a generic hid_register() device).
 *
 * Distinct from hid_device_userdata(), which always returns the caller's
 * own opaque pointer instead - see its own doc. Intended for driving the
 * backend directly (e.g. hw_gpio_set_mode(), hw_wifi_scan()) alongside the
 * events HID already produces for it.
 */
void *hid_device_handle(const hid_device_t *device);

/** @} */

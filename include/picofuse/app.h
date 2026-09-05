/**
 * @file app.h
 * @brief Application bootstrap: a single entry point wrapping the sys
 * lifecycle and run loop.
 * @defgroup Application Application
 * @ingroup Picofuse
 *
 * @code
 * static void on_start(app_t *app, void *userdata) {
 *   hid_t *hid = app_hid(app);
 *   if (hid != NULL) {
 *     hid_register_user_button(hid, KEYCODE_ESC);
 *   }
 * }
 *
 * static void on_event(app_t *app, sys_event_t event, void *userdata) {
 *   // Handle an event posted via sys_runloop_post(), or a hid_event_t
 *   // produced by a device registered in on_start (see hid_event_free()).
 *   if (event == my_exit_event) {
 *     app_shutdown(0);
 *   }
 * }
 *
 * int main(int argc, char *argv[]) {
 *   return app_main(argc, argv, APP_FLAG_NONE, on_start, on_event, NULL);
 * }
 * @endcode
 */
#pragma once
#include "hid.h"
#include "hw.h"
#include "sys.h"

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Feature flags controlling how app_main() runs.
 * @ingroup Application
 */
typedef enum {
  APP_FLAG_NONE = 0,               ///< Default behavior.
  APP_FLAG_MULTICORE = (1 << 0),   ///< Run the event loop across all cores.
  APP_FLAG_SIGNAL = (1 << 1),      ///< Register environment signals (TERM,
                                   ///< INT, QUIT) as HID events, if HID is
                                   ///< available (see @ref app_hid). Has no
                                   ///< effect otherwise.
  APP_FLAG_USER_BUTTON = (1 << 2), ///< Register the board's user button (if
                                   ///< any) as a HID event with keycode
                                   ///< KEYCODE_BUTTON_USER, if HID is
                                   ///< available (see @ref app_hid). Not
                                   ///< every board has a user button; has no
                                   ///< effect when HID is unavailable or the
                                   ///< board has none.
  APP_FLAG_TEMPERATURE = (1 << 3), ///< Register the internal
                                   ///< temperature-sensor channel as a
                                   ///< polling HID metric source (see
                                   ///< hid_register_temperature()), if HID
                                   ///< is available (see @ref app_hid). Has
                                   ///< no effect when HID is unavailable or
                                   ///< the platform has no internal
                                   ///< temperature sensor.
  APP_FLAG_WIFI = (1 << 4),        ///< Register a Wi-Fi connection-state
                                   ///< observer with the default ("XX",
                                   ///< worldwide) country code (see
                                   ///< hid_register_wifi()), if HID is
                                   ///< available (see @ref app_hid). Call
                                   ///< hid_register_wifi() directly instead
                                   ///< of using this flag if a specific
                                   ///< country code is required. Has no
                                   ///< effect when HID is unavailable or the
                                   ///< platform has no Wi-Fi hardware
                                   ///< support built in.
} app_flag_t;

/**
 * @brief Opaque application instance passed to app callbacks.
 * @ingroup Application
 */
typedef struct app_t app_t;

/**
 * @brief Called once, on the main worker, before the run loop starts
 * dispatching events.
 * @ingroup Application
 * @param app Application instance. Valid for the duration of app_main().
 * @param userdata Opaque pointer, as passed to app_main().
 *
 * Use this to complete setup that must run before events can be produced,
 * such as registering HID devices or opening buses.
 */
typedef void (*app_callback_start_t)(app_t *app, void *userdata);

/**
 * @brief Called on a worker for each event the run loop dispatches.
 * @ingroup Application
 * @param app Application instance.
 * @param event Event to handle, as posted via `sys_runloop_post()` (see
 * `sys/runloop.h`).
 * @param userdata Opaque pointer, as passed to app_main().
 */
typedef void (*app_callback_event_t)(app_t *app, sys_event_t event,
                                     void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize the sys subsystem, run the event loop, then tear it
 * down.
 * @ingroup Application
 * @param argc Argument count, as passed to `main()`.
 * @param argv Argument vector, as passed to `main()`.
 * @param flags Feature flags selecting optional behavior (see
 * @ref app_flag_t).
 * @param on_start Called once, on the main worker, before the event loop
 * starts. May be NULL.
 * @param on_event Called for each event the loop dispatches. May be NULL
 * if nothing ever posts events.
 * @param userdata Opaque pointer passed through to @p on_start and
 * @p on_event.
 * @return Exit code, suitable for returning directly from `main()`.
 *
 * Calls `sys_init()`, attempts `hw_init()` and `hid_init()` (see
 * @ref app_hid), initializes the on-board LED if available (see
 * @ref app_led), registers environment signals as HID events if
 * @ref APP_FLAG_SIGNAL is set and HID is available, registers the board's
 * user button as a HID event if @ref APP_FLAG_USER_BUTTON is set and HID is
 * available, registers the internal temperature sensor as a HID metric
 * source if @ref APP_FLAG_TEMPERATURE is set and HID is available, registers
 * a Wi-Fi connection-state observer if @ref APP_FLAG_WIFI is set and HID is
 * available, then runs the event loop across every available core if
 * @ref APP_FLAG_MULTICORE is set, or on the calling thread alone otherwise.
 * Blocks until @ref app_shutdown is called from within a callback (or from
 * another thread), then tears down
 * (`hid_deinit()`, `hw_led_deinit()`, `hw_exit()`, `sys_exit()`).
 */
int app_main(int argc, char *argv[], app_flag_t flags,
             app_callback_start_t on_start, app_callback_event_t on_event,
             void *userdata);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Get the HID instance initialized for this app.
 * @ingroup Application
 * @param app Application instance.
 * @return HID instance, or NULL if the `picofuse-hid` library is not
 * linked into this binary (see @ref app_main).
 */
hid_t *app_hid(const app_t *app);

/**
 * @brief Get the Wi-Fi handle registered for this app.
 * @ingroup Application
 * @param app Application instance.
 * @return Wi-Fi handle, or NULL if @ref APP_FLAG_WIFI was not passed to
 * app_main(), or Wi-Fi is unavailable on this platform.
 *
 * This is the same handle @ref hid_register_wifi() would have returned via
 * `hid_device_userdata()`; call `hw_wifi_scan()`/`hw_wifi_connect()`/
 * `hw_wifi_disconnect()` on it directly to drive the connection.
 */
hw_wifi_t *app_wifi(const app_t *app);

/**
 * @brief Get the on-board LED handle initialized for this app.
 * @ingroup Application
 * @param app Application instance.
 * @return LED handle, or NULL if the platform has no default on-board LED
 * (see hw_led_init_default()), or the picofuse-hw library is not linked
 * into this binary.
 *
 * Always attempted on startup, unlike the optional @ref app_flag_t-gated
 * features; call hw_led_set()/hw_led_blink() on it directly.
 */
hw_led_t *app_led(const app_t *app);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Request that the running app's event loop stop.
 * @ingroup Application
 * @param exit_code Value app_main() returns once the loop has drained and
 * stopped.
 *
 * Safe to call from any callback or thread. Equivalent to
 * `sys_runloop_shutdown()`, exposed here so callers do not need to include
 * `sys/runloop.h` directly.
 */
void app_shutdown(int exit_code);

/** @} */

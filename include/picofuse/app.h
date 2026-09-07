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
 *   return app_main(argc, argv, app_flag_none, on_start, on_event, NULL);
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
  app_flag_none = 0,               ///< Default behavior.
  app_flag_multicore = (1 << 0),   ///< Run the event loop across all cores.
  app_flag_signal = (1 << 1),      ///< Register environment signals (TERM,
                                   ///< INT, QUIT) as HID events (see
                                   ///< @ref app_hid). Has no effect on a
                                   ///< platform with no signal support.
  app_flag_user_button = (1 << 2), ///< Register the board's user button (if
                                   ///< any) as a HID event with keycode
                                   ///< KEYCODE_BUTTON_USER (see
                                   ///< @ref app_hid). Not every board has a
                                   ///< user button; has no effect when the
                                   ///< board has none.
  app_flag_temperature = (1 << 3), ///< Register the internal
                                   ///< temperature-sensor channel as a
                                   ///< polling HID metric source (see
                                   ///< hid_register_temperature() and
                                   ///< @ref app_hid). Has no effect when the
                                   ///< platform has no internal temperature
                                   ///< sensor.
  app_flag_stdio_rtt = (1 << 5),   ///< Initialize standard I/O via SEGGER
                                   ///< RTT (sys_stdio_rtt) instead of the
                                   ///< platform default.
  app_flag_led = (1 << 6),         ///< Initialize the on-board LED if
                                   ///< available (see @ref app_led). Has no
                                   ///< effect when the platform has no
                                   ///< default on-board LED.
  app_flag_watchdog = (1 << 7),    ///< Enable the watchdog (see
                                   ///< @ref app_watchdog) - hw_poll(),
                                   ///< called every run loop tick, feeds
                                   ///< it from there. Has no effect when
                                   ///< the platform has no watchdog
                                   ///< backend.
  app_flag_usb = (1 << 8),         ///< Initialize the USB host subsystem
                                   ///< (see @ref app_usb). No callback is
                                   ///< attached - use hw_usb_set_callback()
                                   ///< or hw_usb_register_hid() on the
                                   ///< returned handle for that. Has no
                                   ///< effect when the platform has no USB
                                   ///< host backend.
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
 * Calls `sys_init()` (with `sys_stdio_rtt` if @ref app_flag_stdio_rtt is set,
 * otherwise the platform default), `hw_init()`, and `hid_init()` (see
 * @ref app_hid), initializes the on-board LED if @ref app_flag_led is set
 * and available (see @ref app_led), registers environment signals as HID
 * events if @ref app_flag_signal is set, registers the board's user button
 * as a HID event if @ref app_flag_user_button is set, registers the
 * internal temperature sensor as a HID metric source if
 * @ref app_flag_temperature is set, enables the watchdog if
 * @ref app_flag_watchdog is set and available (see @ref app_watchdog),
 * then runs the event loop across every available core if
 * @ref app_flag_multicore is set, or on the calling thread alone
 * otherwise. Every run loop tick calls `hw_poll()`, which feeds the
 * watchdog when enabled (see `hw_watchdog_enable()`). Blocks until
 * @ref app_shutdown is called from within a callback (or from another
 * thread), then tears down (`hid_deinit()`, `hw_led_deinit()`,
 * `hw_watchdog_deinit()`, `hw_exit()`, `sys_exit()`).
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
 * @return HID instance. Never NULL once app_main() has called @p on_start
 * (see @ref app_main) - picofuse-app always links picofuse-hid.
 */
hid_t *app_hid(const app_t *app);

/**
 * @brief Get the on-board LED handle initialized for this app.
 * @ingroup Application
 * @param app Application instance.
 * @return LED handle, or NULL if @ref app_flag_led was not passed to
 * app_main(), or the platform has no default on-board LED (see
 * hw_led_init_default()).
 *
 * Call hw_led_set()/hw_led_blink() on it directly.
 */
hw_led_t *app_led(const app_t *app);

/**
 * @brief Get the watchdog handle initialized for this app.
 * @ingroup Application
 * @param app Application instance.
 * @return Watchdog handle, or NULL if @ref app_flag_watchdog was not
 * passed to app_main(), or the platform has no watchdog backend (see
 * hw_watchdog_init()).
 *
 * app_main() already calls hw_watchdog_enable() to start feeding this from
 * hw_poll() (see its own doc) - call hw_watchdog_reset() on it directly to
 * force an earlier reset, or hw_watchdog_enable() to stop feeding it.
 */
hw_watchdog_t *app_watchdog(const app_t *app);

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

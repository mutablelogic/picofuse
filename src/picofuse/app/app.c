#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

#if defined(SYSTEM_NAME_PICO)
// Defined in hw/pico/init.c - internal, not part of the public hw API.
extern bool _hw_requires_single_core(void);
#endif

/**
 * @def APP_QUEUE_CAPACITY
 * @brief Maximum number of events retained by an app's event queue.
 */
#ifndef APP_QUEUE_CAPACITY
#define APP_QUEUE_CAPACITY 32
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct app_t {
  sys_event_queue_t *queue;
  hid_t *hid;
  hw_led_t *led;
  hw_watchdog_t *watchdog;
  hw_usb_t *usb;
  app_flag_t flags;
  app_callback_start_t on_start;
  app_callback_event_t on_event;
  void *userdata;
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

// application-wide singleton
static app_t *_app = NULL;

static void _app_on_init(uint8_t worker) {
  if (worker != 0u) {
    return;
  }

  sys_debugf("app", "%s version=%s (system=%s serial=%s)", sys_env_name(),
             sys_env_version(), sys_env_system(), sys_env_serial());

  // Initialize the hardware and HID subsystems.
  hw_init();
  _app->hid = hid_init(_app->queue);
  if (_app->hid) {
    sys_debugf("app", "app_flag_hid enabled");
  }

  // hw_led_init_default()
  if (_app->flags & app_flag_led) {
    _app->led = hw_led_init_default();
  }
  if (_app->led) {
    sys_debugf("app", "app_flag_led enabled");
    hw_led_clear(_app->led);
  } else if (_app->flags & app_flag_led) {
    sys_debugf("app", "app_flag_led not supported");
  }

  if (_app->flags & app_flag_watchdog) {
    _app->watchdog = hw_watchdog_init();
  }
  if (_app->watchdog != NULL) {
    hw_watchdog_enable(_app->watchdog, true);
    sys_debugf("app", "app_flag_watchdog enabled");
    if (hw_watchdog_did_reset(_app->watchdog)) {
      sys_debugf("watchdog", "previous reset was watchdog-triggered");
    }
  } else if (_app->flags & app_flag_watchdog) {
    sys_debugf("app", "app_flag_watchdog not supported");
  }

  if (_app->flags & app_flag_usb) {
    _app->usb = hw_usb_init();
  }

  // usb -> HID bridge
  if (_app->hid != NULL && (_app->flags & app_flag_usb)) {
    if (_app->usb != NULL &&
        hw_usb_register_hid(_app->hid, _app->usb) != NULL) {
      sys_debugf("app", "app_flag_usb enabled");
    } else {
      sys_debugf("app", "app_flag_usb not supported");
    }
  }

  // signals
  if (_app->hid != NULL && (_app->flags & app_flag_signal)) {
    if (hid_register_signal(_app->hid, NULL)) {
      sys_debugf("app", "app_flag_signal enabled");
    } else {
      sys_debugf("app", "app_flag_signal not supported");
    }
  }

  // user button
  if (_app->hid != NULL && (_app->flags & app_flag_user_button)) {
    if (hid_register_user_button(_app->hid, KEYCODE_BUTTON_USER, NULL)) {
      sys_debugf("app", "app_flag_user_button enabled");
    } else {
      sys_debugf("app", "app_flag_user_button not supported");
    }
  }

  // internal temperature sensor
  if (_app->hid != NULL && (_app->flags & app_flag_temperature)) {
    if (hid_register_temperature(_app->hid, 0u, NULL)) {
      sys_debugf("app", "app_flag_temperature enabled");
    } else {
      sys_debugf("app", "app_flag_temperature not supported");
    }
  }

  // callback for app start
  if (_app->on_start != NULL) {
    _app->on_start(_app, _app->userdata);
  }
}

static void _app_on_event(sys_event_t event) {
  if (_app->on_event != NULL) {
    _app->on_event(_app, event, _app->userdata);
  }
}

static void _app_poll(void) {
  hw_poll();
  if (_app->hid != NULL) {
    (void)hid_poll(_app->hid);
  }
}

static void _app_on_exit(uint8_t worker) {
  if (worker != 0u) {
    return;
  }

  hid_deinit(_app->hid);
  _app->hid = NULL;

  hw_led_deinit(_app->led);
  _app->led = NULL;

  hw_watchdog_deinit(_app->watchdog);
  _app->watchdog = NULL;

  hw_usb_deinit(_app->usb);
  _app->usb = NULL;

  hw_exit();
}

// True if app_flag_multicore was requested but isn't actually safe to
// honor on this platform - logs why when it overrides the caller. Only
// Pico's CYW43 driver has this constraint today (see
// _hw_requires_single_core()'s own doc in hw/pico/init.c), so this is a
// no-op everywhere else.
static bool _app_single_core_required(app_flag_t flags) {
  if ((flags & app_flag_multicore) == 0) {
    return false;
  }
#if defined(SYSTEM_NAME_PICO)
  if (_hw_requires_single_core()) {
    sys_debugf("app", "app_flag_multicore requested but this platform's "
                      "hardware backend requires single-core operation - "
                      "falling back to a single core");
    return true;
  }
#endif
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

int app_main(int argc, char *argv[], app_flag_t flags,
             app_callback_start_t on_start, app_callback_event_t on_event,
             void *userdata) {
  sys_init(argc, argv, 0,
           (flags & app_flag_stdio_rtt) ? sys_stdio_rtt : sys_stdio_none);

  sys_event_queue_t *queue = sys_event_queue_init(APP_QUEUE_CAPACITY);
  sys_assert(queue != NULL);

  app_t app = {
      .queue = queue,
      .hid = NULL,
      .led = NULL,
      .watchdog = NULL,
      .usb = NULL,
      .flags = flags,
      .on_start = on_start,
      .on_event = on_event,
      .userdata = userdata,
  };
  _app = &app;

  // Run on all cores if app_flag_multicore is set, otherwise run on a
  // single core - unless _app_single_core_required() overrides that (see
  // its own doc), in which case that always wins over what the caller
  // asked for.
  uint8_t num_workers =
      (flags & app_flag_multicore) && !_app_single_core_required(flags) ? 0u
                                                                        : 1u;

  // Run the event loop until app_shutdown() is called, then exit with the
  // provided exit code.
  uint32_t exit_code = sys_runloop_run(num_workers, queue, _app_on_init,
                                       _app_on_event, _app_poll, _app_on_exit);

  sys_event_queue_deinit(queue);
  _app = NULL;
  sys_exit();
  return (int)exit_code;
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

hid_t *app_hid(const app_t *app) { return (app != NULL) ? app->hid : NULL; }

hw_led_t *app_led(const app_t *app) { return (app != NULL) ? app->led : NULL; }

hw_watchdog_t *app_watchdog(const app_t *app) {
  return (app != NULL) ? app->watchdog : NULL;
}

hw_usb_t *app_usb(const app_t *app) { return (app != NULL) ? app->usb : NULL; }

///////////////////////////////////////////////////////////////////////////////
// METHODS

void app_shutdown(int exit_code) { sys_runloop_shutdown((uint32_t)exit_code); }

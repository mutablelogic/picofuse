#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

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
  hw_wifi_t *wifi;
  hw_led_t *led;
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

  // Initialize the hardware and HID subsystems.
  hw_init();
  _app->hid = hid_init(_app->queue);
  if (_app->hid) {
    sys_debugf("app", "app_flag_hid");
  }

  // hw_led_init_default()
  if (_app->flags & app_flag_led) {
    _app->led = hw_led_init_default();
  }
  if (_app->led) {
    sys_debugf("app", "app_flag_led");
  }

  // signals
  if (_app->hid != NULL && (_app->flags & app_flag_signal)) {
    if (hid_register_signal(_app->hid, NULL)) {
      sys_debugf("app", "app_flag_signal");
    }
  }

  // user button
  if (_app->hid != NULL && (_app->flags & app_flag_user_button)) {
    if (hid_register_user_button(_app->hid, KEYCODE_BUTTON_USER, NULL)) {
      sys_debugf("app", "app_flag_user_button");
    }
  }

  // internal temperature sensor
  if (_app->hid != NULL && (_app->flags & app_flag_temperature)) {
    if (hid_register_temperature(_app->hid, 0u, NULL)) {
      sys_debugf("app", "app_flag_temperature");
    }
  }

  // wifi
  if (_app->hid != NULL && (_app->flags & app_flag_wifi)) {
    _app->wifi = hw_wifi_init_client("XX");
    if (_app->wifi != NULL &&
        hid_register_wifi(_app->hid, _app->wifi, NULL) == NULL) {
      hw_wifi_deinit(_app->wifi);
      _app->wifi = NULL;
    }
  }
  if (_app->wifi) {
    sys_debugf("app", "app_flag_wifi");
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

  // hid_deinit() only detaches the callback it attached to _app->wifi (see
  // hid_register_wifi()'s own doc) - it does not bring the radio down, so
  // that's still this app's own responsibility below, mirroring who
  // brought it up in _app_on_init().
  hid_deinit(_app->hid);
  _app->hid = NULL;

  if (_app->wifi != NULL) {
    hw_wifi_deinit(_app->wifi);
    _app->wifi = NULL;
  }

  hw_led_deinit(_app->led);
  _app->led = NULL;

  hw_exit();
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
      .wifi = NULL,
      .led = NULL,
      .flags = flags,
      .on_start = on_start,
      .on_event = on_event,
      .userdata = userdata,
  };
  _app = &app;

  // Run on all cores if app_flag_multicore is set, otherwise run on a
  // single core.
  uint8_t num_workers = (flags & app_flag_multicore) ? 0u : 1u;

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

hw_wifi_t *app_wifi(const app_t *app) {
  return (app != NULL) ? app->wifi : NULL;
}

hw_led_t *app_led(const app_t *app) { return (app != NULL) ? app->led : NULL; }

///////////////////////////////////////////////////////////////////////////////
// METHODS

void app_shutdown(int exit_code) { sys_runloop_shutdown((uint32_t)exit_code); }

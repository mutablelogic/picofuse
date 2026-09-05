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

/**
 * @def APP_WIFI_QUEUE_CAPACITY
 * @brief Maximum number of hid_event_type_wifi events held for worker 0
 * (see _app_on_event()).
 */
#ifndef APP_WIFI_QUEUE_CAPACITY
#define APP_WIFI_QUEUE_CAPACITY 8
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct app_t {
  sys_event_queue_t *queue;
  // Holds hid_event_type_wifi events popped on a non-zero core, so they can
  // be redispatched from worker 0 (see _app_on_event()/_app_poll()).
  sys_event_queue_t *wifi_queue;
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

// sys_runloop is itself a process-wide singleton, so a single static
// instance mirrors that rather than adding lifetime management app_main()
// does not need.
static app_t *_app = NULL;

static void _app_on_init(uint8_t worker) {
  if (worker != 0u) {
    return;
  }

  // hw_init() and hid_init() are weakly linked (see hw.c and hid.c): they
  // are harmless no-ops (hid_init() returning NULL) unless the
  // picofuse-hw / picofuse-hid libraries are also linked into this binary,
  // so a NULL app_hid() is expected, not an error, when that library is
  // absent.
  hw_init();
  _app->hid = hid_init(_app->queue);

  // hw_led_init_default() is weakly linked (see hw.c), so a NULL app_led()
  // is expected, not an error, when picofuse-hw is absent or the platform
  // has no default on-board LED. Always attempted, unlike the flag-gated
  // features below.
  _app->led = hw_led_init_default();

  if (_app->hid != NULL && (_app->flags & APP_FLAG_SIGNAL)) {
    (void)hid_register_signal(_app->hid, NULL);
  }

  // hid_register_user_button() returns NULL when the board has no user
  // button, which is expected, not an error.
  if (_app->hid != NULL && (_app->flags & APP_FLAG_USER_BUTTON)) {
    (void)hid_register_user_button(_app->hid, KEYCODE_BUTTON_USER, NULL);
  }

  // hid_register_temperature() returns NULL when the platform has no
  // internal temperature sensor, which is expected, not an error.
  if (_app->hid != NULL && (_app->flags & APP_FLAG_TEMPERATURE)) {
    (void)hid_register_temperature(_app->hid, 0u, NULL);
  }

  // hw_wifi_init_client() returns NULL when the platform has no Wi-Fi
  // hardware support built in, which is expected, not an error.
  // hid_register_wifi() only observes an already-initialized handle (see
  // its own doc) - app_main() is the one that brings the radio up here,
  // and is responsible for hw_wifi_deinit() on it in _app_on_exit() below.
  if (_app->hid != NULL && (_app->flags & APP_FLAG_WIFI)) {
    _app->wifi = hw_wifi_init_client("XX");
    if (_app->wifi != NULL &&
        hid_register_wifi(_app->hid, _app->wifi, NULL) == NULL) {
      hw_wifi_deinit(_app->wifi);
      _app->wifi = NULL;
    }
  }

  if (_app->on_start != NULL) {
    _app->on_start(_app, _app->userdata);
  }
}

// picofuse-hw's Pico Wi-Fi backend links pico_cyw43_arch_lwip_poll, which
// provides no cross-core safety: any cyw43/lwIP call (hw_wifi_scan()/
// _connect()/_disconnect(), etc.) made from a core other than the one
// hw_init() ran on (core 0, see _app_on_init()) panics. hw_poll() itself is
// always safe - sys_runloop_run() only ever calls its poll_fn from worker
// 0's own loop, never from an additional worker (see
// sys_runloop_poll_func_t's own doc) - but event_fn is not: every worker
// races to pop the same queue, so with APP_FLAG_MULTICORE a
// hid_event_type_wifi event can just as easily be dispatched to this
// callback on a non-zero core, and it's entirely reasonable for an app's
// on_event() to call back into hw_wifi_*() straight from a wifi event. So a
// wifi event landing here on a non-zero core is queued instead of
// dispatched directly, and redelivered from worker 0 by _app_poll() below.
static void _app_on_event(sys_event_t event) {
  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event != NULL && hid_event->type == hid_event_type_wifi &&
      sys_thread_core() != 0u) {
    if (!sys_event_queue_try_push(_app->wifi_queue, event)) {
      sys_debugf("app", "wifi event queue full, dropping event");
      hid_event_free(hid_event);
    }
    return;
  }

  if (_app->on_event != NULL) {
    _app->on_event(_app, event, _app->userdata);
  }
}

static void _app_poll(void) {
  // Guaranteed to run on worker 0 only (see sys_runloop_poll_func_t's own
  // doc), so it's safe to redeliver wifi events deferred by
  // _app_on_event() here.
  sys_event_t deferred;
  while ((deferred = sys_event_queue_try_pop(_app->wifi_queue)) != NULL) {
    if (_app->on_event != NULL) {
      _app->on_event(_app, deferred, _app->userdata);
    }
  }

  hw_poll();
  if (_app->hid != NULL) {
    (void)hid_poll(_app->hid);
  }
}

static void _app_on_exit(uint8_t worker) {
  if (worker != 0u) {
    return;
  }

  // Free any wifi events deferred by _app_on_event() that never reached
  // _app_poll() before shutdown, so nothing leaks.
  sys_event_t deferred;
  while ((deferred = sys_event_queue_try_pop(_app->wifi_queue)) != NULL) {
    hid_event_free((hid_event_t *)deferred);
  }
  sys_event_queue_deinit(_app->wifi_queue);
  _app->wifi_queue = NULL;

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
  sys_init(argc, argv, 0, sys_stdio_none);

  sys_event_queue_t *queue = sys_event_queue_init(APP_QUEUE_CAPACITY);
  sys_assert(queue != NULL);

  sys_event_queue_t *wifi_queue =
      sys_event_queue_init(APP_WIFI_QUEUE_CAPACITY);
  sys_assert(wifi_queue != NULL);

  app_t app = {
      .queue = queue,
      .wifi_queue = wifi_queue,
      .hid = NULL,
      .wifi = NULL,
      .led = NULL,
      .flags = flags,
      .on_start = on_start,
      .on_event = on_event,
      .userdata = userdata,
  };
  _app = &app;

  // Run on all cores if APP_FLAG_MULTICORE is set, otherwise run on a
  // single core.
  uint8_t num_workers = (flags & APP_FLAG_MULTICORE) ? 0u : 1u;

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

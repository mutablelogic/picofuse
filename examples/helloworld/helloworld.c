#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

static bool _led_on = false;

// Wi-Fi is application-managed for now, not app_main()'s job - own the
// handle here so hid_event_type_wifi events keep flowing for the demo
// below, purely as an observer (never actually joins or scans).
static hw_wifi_t *_wifi = NULL;

static const char *_wifi_event_label(hw_wifi_event_t event, bool has_network) {
  if (event & hw_wifi_event_scan) {
    return has_network ? "scan result" : "scan complete";
  } else if (event & hw_wifi_event_joining) {
    return "joining";
  } else if (event & hw_wifi_event_connected) {
    return "connected";
  } else if (event & hw_wifi_event_disconnected) {
    return "disconnected";
  } else if (event & hw_wifi_event_badauth) {
    return "bad auth";
  } else if (event & hw_wifi_event_notfound) {
    return "not found";
  } else if (event & hw_wifi_event_error) {
    return "error";
  } else if (event & hw_wifi_event_status) {
    return "status";
  }
  return "unknown";
}

static void _on_start(app_t *app, void *userdata) {
  (void)userdata;
  sys_puts("Hello, world!\n");

  _wifi = hw_wifi_init_client("XX");
  if (_wifi != NULL && hid_register_wifi(app_hid(app), _wifi, NULL) == NULL) {
    hw_wifi_deinit(_wifi);
    _wifi = NULL;
  }
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  char state_buf[128];

  switch (hid_event->type) {
  case hid_event_type_keycode:
    hid_state_to_string(hid_event->data.keycode.state, state_buf,
                        sizeof(state_buf));
    sys_printf("Keycode event: %s state: %s\n",
               hid_keycode_to_string(hid_event->data.keycode.keycode),
               state_buf);

    // Toggle the on-board LED on each user-button press - ignoring the
    // release means holding it down doesn't flicker. app_led() is NULL-safe
    // to pass to hw_led_set() if this platform has no default LED.
    if ((hid_event->data.keycode.state & hid_state_on) != 0) {
      _led_on = !_led_on;
      hw_led_set(app_led(app), 0, _led_on);
    }
    break;
  case hid_event_type_signal:
    sys_printf("Signal event: %u\n", (unsigned)hid_event->data.signal.signal);

    // Ctrl-C/SIGTERM on a host build - a Pico board has no such signals, so
    // this branch never fires there; app_shutdown() is only reachable by
    // physically resetting the board instead.
    if (_wifi != NULL) {
      hw_wifi_deinit(_wifi);
      _wifi = NULL;
    }
    app_shutdown(0);
    break;

  case hid_event_type_metric:
    sys_printf("Metric event: %s=%.2f %s\n", hid_event->data.metric.name,
               (double)hid_event->data.metric.value,
               hid_event->data.metric.unit);
    break;

  case hid_event_type_wifi:
    if (hid_event->data.wifi.has_network) {
      sys_printf("Wi-Fi event: %s ssid=%s rssi=%d\n",
                 _wifi_event_label(hid_event->data.wifi.event, true),
                 hid_event->data.wifi.network.ssid,
                 (int)hid_event->data.wifi.network.rssi);
    } else {
      sys_printf("Wi-Fi event: %s\n",
                 _wifi_event_label(hid_event->data.wifi.event, false));
    }
    break;

  case hid_event_type_touch:
    hid_state_to_string(hid_event->data.touch.state, state_buf,
                        sizeof(state_buf));
    sys_printf("Touch event: %s slot=%u x=%d y=%d\n", state_buf,
               (unsigned)hid_event->data.touch.slot,
               (int)hid_event->data.touch.point.x,
               (int)hid_event->data.touch.point.y);
    break;

  default:
    break;
  }

  hid_event_free(hid_event);
}

int main(int argc, char *argv[]) {
  // Every flag on for testing, including app_flag_multicore. on_event()
  // below calls hw_led_set() straight from a keycode event regardless of
  // which core it lands on - safe even when the default LED is wired
  // through the Wi-Fi chip, since cyw43_arch's threadsafe_background
  // context (what this project builds against) serializes every call
  // into the driver internally, from any core.
  return app_main(argc, argv,
                  app_flag_stdio_rtt | app_flag_multicore | app_flag_led |
                      app_flag_signal | app_flag_user_button |
                      app_flag_temperature,
                  _on_start, _on_event, NULL);
}

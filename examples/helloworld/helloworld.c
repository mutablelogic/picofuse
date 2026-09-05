#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

static bool _led_on = false;

static void _on_start(app_t *app, void *userdata) {
  (void)app;
  (void)userdata;
  sys_puts("Hello, world!\n");
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  switch (hid_event->type) {
  case hid_event_type_keycode:
    // Toggle the on-board LED on each user-button press - ignoring the
    // release means holding it down doesn't flicker. app_led() is NULL-safe
    // to pass to hw_led_set() if this platform has no default LED.
    if ((hid_event->data.keycode.state & hid_state_on) != 0) {
      _led_on = !_led_on;
      hw_led_set(app_led(app), 0, _led_on);
    }
    break;
  case hid_event_type_signal:
    // Ctrl-C/SIGTERM on a host build - a Pico board has no such signals, so
    // this branch never fires there; app_shutdown() is only reachable by
    // physically resetting the board instead.
    app_shutdown(0);
    break;
  default:
    break;
  }

  hid_event_free(hid_event);
}

int main(int argc, char *argv[]) {
  // APP_FLAG_USER_BUTTON is a no-op on boards/platforms with no user
  // button (see hid_register_user_button()) - nothing else to gate here.
  return app_main(argc, argv, APP_FLAG_SIGNAL | APP_FLAG_USER_BUTTON,
                  _on_start, _on_event, NULL);
}

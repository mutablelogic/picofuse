#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <test/test.h>

// app_flag_usb: app_main() itself calls hw_usb_init() + hw_usb_register_hid()
// during on_init(), before on_start() runs (see app.h's own doc) - this
// confirms the resulting hid_event_type_usb events actually reach the
// app's own on_event() dispatch once the run loop starts ticking
// (hw_poll(), called every tick - see app_flag_watchdog's own doc for
// that guarantee), the same events hid_010 observes by calling
// hw_usb_register_hid() by hand. Skips cleanly if this platform/build has
// no USB host backend - see test/hw_023 for why.
//
// Passes _on_event to test_main_app() (see its own doc): events only
// reach the app after on_start() returns and the run loop starts
// ticking, so a NULL-on_event (synchronous) test body can't observe
// them. A one-shot hid_register_timer() stands in for hid_010's own
// manual polling-loop timeout, since this test only reacts to events the
// run loop dispatches - it has no loop of its own to poll or sleep in.
#define APP_002_TIMEOUT_MS (5 * 1000)

static hid_device_t *_timeout_device = NULL;
static int _attach_count = 0;

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;

  bool timed_out = hid_event->type == hid_event_type_timer &&
                   hid_event->device == _timeout_device;
  test_assert(!timed_out);

  if (hid_event->type != hid_event_type_usb) {
    hid_event_free(hid_event);
    return;
  }

  if (hid_event->data.usb.has_device) {
    if (hid_event->data.usb.event == hw_usb_event_attached) {
      _attach_count++;
      // Enumeration happens at the interface level (see hw/usb.h's own
      // top-level doc) - a device with N interfaces fires this event N
      // times, sharing device_id/vid/pid but each with its own
      // interface_number/interface_class.
      const hw_usb_device_t *d = &hid_event->data.usb.device;
      if (d->interface_number == 0xFF) {
        sys_printf("[app_002] attached: device_id=%u vid=%04x pid=%04x "
                   "interface=n/a\n",
                   (unsigned)d->device_id, d->vid, d->pid);
      } else {
        sys_printf("[app_002] attached: device_id=%u vid=%04x pid=%04x "
                   "interface=%u class=%s\n",
                   (unsigned)d->device_id, d->vid, d->pid,
                   d->interface_number,
                   hw_usb_device_class_to_string(d->interface_class));
      }
    }
    hid_event_free(hid_event);
    return;
  }

  // Enumeration complete.
  sys_printf("[app_002] enumeration complete: %d interface event(s)\n",
             _attach_count);
  sys_printf("[TEST] [EXIT] %s\n", sys_env_name());
  hid_event_free(hid_event);
  hid_deregister(app_hid(app), _timeout_device);
  app_shutdown(0);
}

test_main_app(app_flag_usb, _on_event) {
  if (app_usb(app) == NULL) {
    sys_printf("[app_002] no USB host backend on this platform\n");
    sys_printf("[TEST] [EXIT] %s\n", sys_env_name());
    app_shutdown(0);
    return;
  }

  _timeout_device =
      hid_register_timer(app_hid(app), 0, APP_002_TIMEOUT_MS, false, NULL);
  test_assert(_timeout_device != NULL);
}

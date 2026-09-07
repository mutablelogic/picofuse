#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_usb_register_hid(): HID attaches to an already-initialized hw_usb_t*
// (it does not create or own it - see hw/usb.h's own doc) and forwards
// every hotplug event as a hid_event_type_usb event. Skips cleanly if this
// platform/build has no USB host backend (hw_usb_init() returns NULL) -
// see test/hw_023 for why (stub by default, real libusb/TinyUSB backend
// only with PICOFUSE_USB=ON).
#define HID_USB_TEST_ENUM_TIMEOUT_MS (5 * 1000)
#define HID_USB_TEST_POLL_MS 50

test_main_hw(0) {
  hw_usb_t *usb = hw_usb_init();
  if (usb == NULL) {
    sys_printf("[hid_010] no USB host backend on this platform\n");
    return;
  }

  sys_event_queue_t *queue = sys_event_queue_init(32);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  hid_device_t *device = hw_usb_register_hid(instance, usb);
  test_assert(device != NULL);

  // A second registration is refused - hw_usb_register_hid() is a
  // singleton, same as hw_usb_t itself (see its own doc).
  test_assert(hw_usb_register_hid(instance, usb) == NULL);

  const char *name = NULL;
  hid_type_t type = hid_type_none;
  test_assert(hid_device_info(device, &name, NULL, &type, NULL));
  test_assert_strequal(name, "usb");
  test_assert(type == hid_type_usb);

  // Unlike hid_register_wifi() (compiled into picofuse-hid itself, so it
  // can stash wifi in device->context - see hid_device_handle()'s own
  // doc), hw_usb_register_hid() lives outside picofuse-hid (see
  // hw/usb.h's own doc on why) and only has the public API to work with -
  // there's no handle to retrieve here, since the caller already has usb
  // in hand (they're the one who passed it in).
  test_assert(hid_device_handle(device) == NULL);

  sys_printf("[hid_010] waiting for initial USB enumeration...\n");

  bool got_enum_complete = false;
  int attach_count = 0;
  uint64_t start = sys_timestamp_ms();
  while (!got_enum_complete &&
        sys_timestamp_ms() - start < HID_USB_TEST_ENUM_TIMEOUT_MS) {
    hw_poll();
    hid_event_t *event = (hid_event_t *)sys_event_queue_try_pop(queue);
    if (event != NULL) {
      test_assert(event->type == hid_event_type_usb);
      test_assert(event->device == device);
      if (!event->data.usb.has_device) {
        got_enum_complete = true;
      } else if (event->data.usb.event == hw_usb_event_attached) {
        attach_count++;
        // Enumeration happens at the interface level (see hw/usb.h's own
        // top-level doc) - a device with N interfaces fires this event N
        // times, sharing device_id/vid/pid but each with its own
        // interface_number/interface_class.
        const hw_usb_device_t *d = &event->data.usb.device;
        if (d->interface_number == 0xFF) {
          sys_printf("[hid_010] attached: device_id=%u vid=%04x pid=%04x "
                     "interface=n/a\n",
                     (unsigned)d->device_id, d->vid, d->pid);
        } else {
          sys_printf("[hid_010] attached: device_id=%u vid=%04x pid=%04x "
                     "interface=%u class=%s\n",
                     (unsigned)d->device_id, d->vid, d->pid,
                     d->interface_number,
                     hw_usb_device_class_to_string(d->interface_class));
        }
      }
      hid_event_free(event);
    } else {
      sys_sleep_ms(HID_USB_TEST_POLL_MS);
    }
  }
  test_assert(got_enum_complete);
  sys_printf("[hid_010] enumeration complete: %d interface event(s)\n",
             attach_count);

  test_assert(hid_deregister(instance, device));

  // hid_deregister() only detached the callback - usb is still the
  // caller's to manage. If hid_deregister() had wrongly called
  // hw_usb_deinit() itself, this would double-free/crash.
  hw_usb_deinit(usb);

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}

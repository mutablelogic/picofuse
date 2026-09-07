#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

static int _attach_count = 0;
static int _enum_complete = 0;

static void _hw_023_callback(hw_usb_t *usb, hw_usb_event_t event,
                             const hw_usb_device_t *device, void *userdata) {
  (void)usb;
  (void)userdata;

  if (device == NULL) {
    // Enumeration-complete marker (see hw_usb_init()'s own doc).
    _enum_complete = 1;
    return;
  }
  if (event == hw_usb_event_attached) {
    _attach_count++;
    // Enumeration happens at the interface level (see hw/usb.h's own
    // top-level doc) - a device with N interfaces fires this callback N
    // times, sharing device_id/vid/pid but each with its own
    // interface_number/interface_class.
    if (device->interface_number == 0xFF) {
      sys_printf("[hw_023] attached: device_id=%u vid=%04x pid=%04x "
                 "interface=n/a\n",
                 (unsigned)device->device_id, device->vid, device->pid);
    } else {
      sys_printf("[hw_023] attached: device_id=%u vid=%04x pid=%04x "
                 "interface=%u class=%s\n",
                 (unsigned)device->device_id, device->vid, device->pid,
                 device->interface_number,
                 hw_usb_device_class_to_string(device->interface_class));
    }
  }
}

// hw_usb_device_class_to_string(): symbolic name in debug builds, "0x%02X"
// fallback for an unrecognized code (and unconditionally once NDEBUG is
// defined) - same idiom as hid_002/main.c's own hid_keycode_to_string()
// test.
static void _test_class_to_string(void) {
#ifndef NDEBUG
  test_assert_strequal(hw_usb_device_class_to_string(hw_usb_device_class_hid),
                       "hw_usb_device_class_hid");
  test_assert_strequal(
      hw_usb_device_class_to_string(hw_usb_device_class_vendor_specific),
      "hw_usb_device_class_vendor_specific");
#endif
  // 0x11 ("Billboard") isn't a named class - always the hex fallback.
  test_assert_strequal(
      hw_usb_device_class_to_string((hw_usb_device_class_t)0x11), "0x11");
}

// hw_usb_* NULL-safety, plus the init/enumerate/deinit lifecycle on
// whatever backend this build actually has - the stub by default
// (PICOFUSE_USB is off), or a real one if PICOFUSE_USB=ON: libusb on
// Linux/Darwin (if libusb-1.0 was found - see hw/{linux,darwin}/
// CMakeLists.txt), TinyUSB host on Pico (see hw/pico/CMakeLists.txt).
// Exits cleanly rather than failing if no USB host backend is available -
// there's nothing to exercise, not a test failure (same tolerance as
// hw_011/hw_021's own "no hardware available" paths).
test_main_hw(0) {
  _test_class_to_string();

  // NULL-safety: every operation must tolerate an invalid handle/argument.
  hw_usb_deinit(NULL);                      // must not crash
  hw_usb_set_callback(NULL, NULL, NULL);    // must not crash

  hw_usb_t *usb = hw_usb_init();
  if (usb == NULL) {
    sys_printf("[hw_023] no USB host backend available on this platform\n");
    return;
  }

  // Init and callback attachment are deliberately separate calls - see
  // hw_usb_set_callback()'s own doc.
  hw_usb_set_callback(usb, _hw_023_callback, NULL);

  // The backend may enumerate on its own background thread (libusb) or
  // defer to hw_poll() (TinyUSB) - poll for a bit either way.
  for (int i = 0; i < 20 && !_enum_complete; i++) {
    hw_poll();
    sys_sleep_ms(50);
  }
  sys_printf("[hw_023] usb: attach_count=%d enum_complete=%d\n", _attach_count,
             _enum_complete);
  // Unconditional per hw_usb_init()'s own doc, regardless of whether
  // anything was actually attached to enumerate.
  test_assert(_enum_complete);

  hw_usb_deinit(usb);

  // The singleton is free again once deinited - a fresh init must succeed.
  hw_usb_t *usb2 = hw_usb_init();
  test_assert(usb2 != NULL);
  hw_usb_deinit(usb2);
}

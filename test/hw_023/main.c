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
// whatever backend this build actually has - the stub on Darwin/Linux by
// default (PICOFUSE_USB is off), a real libusb backend if PICOFUSE_USB=ON
// and libusb-1.0 was found (see hw/{linux,darwin}/CMakeLists.txt), or
// eventually TinyUSB on Pico. Exits cleanly rather than failing if no USB
// host backend is available - there's nothing to exercise, not a test
// failure (same tolerance as hw_011/hw_021's own "no hardware available"
// paths).
test_main_hw(0) {
  _test_class_to_string();

  // NULL-safety: every operation must tolerate an invalid handle/argument.
  hw_usb_deinit(NULL); // must not crash
  test_assert(hw_usb_init(NULL, NULL) == NULL);

  hw_usb_t *usb = hw_usb_init(_hw_023_callback, NULL);
  if (usb == NULL) {
    sys_printf("[hw_023] no USB host backend available on this platform\n");
    return;
  }

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
  hw_usb_t *usb2 = hw_usb_init(_hw_023_callback, NULL);
  test_assert(usb2 != NULL);
  hw_usb_deinit(usb2);
}

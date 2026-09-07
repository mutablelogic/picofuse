#include <picofuse/app.h>
#include <picofuse/sys.h>
#include <test/test.h>

test_main_app(app_flag_signal | app_flag_user_button | app_flag_temperature |
             app_flag_usb,
             NULL) {
  test_assert(app != NULL);

  // picofuse-hid is linked into this test (see app_001's LIBRARIES below),
  // so the real hid_init() should have run, not hw.c/hid.c's weak
  // fallbacks - see app_main()'s own doc.
  test_assert(app_hid(app) != NULL);

  // Not asserted non-NULL: whether this platform/build has a default
  // on-board LED, or a USB host backend, is unrelated to what this test
  // is checking - just that app_flag_usb's own hw_usb_init() +
  // hw_usb_register_hid() (see app_main()'s own doc) didn't crash either
  // way.
  (void)app_led(app);
  (void)app_usb(app);
}

#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hid_register_user_button(): a real register/handle/userdata/deregister
// round trip against the board's known user-button pin macro
// (PICO_USER_SW_PIN / PIMORONI_PICO_LIPO2_USER_SW_PIN - see userbutton.c).
// Host-only builds never define one of these (they only ever come from a
// pico-sdk board header), so this is Pico-only - there's nothing
// meaningful to assert on a host build.
test_main_hw(0) {
  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  int userdata_sentinel = 0;
  hid_device_t *device = hid_register_user_button(instance, KEYCODE_ENTER,
                                                  &userdata_sentinel);
  test_assert(device != NULL);

  const char *name = NULL;
  uint32_t id = 0;
  hid_type_t type = hid_type_none;
  hid_class_t hid_class = hid_class_unknown;
  test_assert(hid_device_info(device, &name, &id, &type, &hid_class));
  test_assert(type == hid_type_gpio);

  test_assert(hid_device_userdata(device) == &userdata_sentinel);

  hw_gpio_t *gpio = (hw_gpio_t *)hid_device_handle(device);
  test_assert(gpio != NULL);
  test_assert(hw_gpio_bank(gpio) == 0);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_pullup);

  test_assert(hid_deregister(instance, device));
  test_assert(!hid_device_info(device, NULL, NULL, NULL, NULL));

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}

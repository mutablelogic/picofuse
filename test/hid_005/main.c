#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hid_register_gpio_input(): NULL on a platform with no GPIO hardware at
// all (Darwin's stub backend - hw_gpio_count() reports 0, same guard as
// test/hw_001), and a full register/handle/userdata/deregister round trip
// on a platform that does have one. This only exercises the raw
// input mode - not hid_register_gpio_pullup()/_pulldown(), which share
// the same internal _hid_register_gpio_mode() path, or edge delivery
// itself, which needs a real driven pin to observe.
test_main_hw(0) {
  uint8_t bank = 0;
  uint8_t pin = 0;
  int userdata_sentinel = 0;

  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  uint8_t count = hw_gpio_count(bank);
  sys_debugf("hid_005", "hw_gpio_count(%u) = %u", bank, count);
  if (count == 0) {
    test_assert(hid_register_gpio_input(instance, bank, pin, KEYCODE_A,
                                        NULL) == NULL);
    hid_deinit(instance);
    sys_event_queue_deinit(queue);
    return;
  }

  hid_device_t *device = hid_register_gpio_input(instance, bank, pin,
                                                 KEYCODE_A,
                                                 &userdata_sentinel);
  test_assert(device != NULL);

  const char *name = NULL;
  uint32_t id = 0;
  hid_type_t type = hid_type_none;
  hid_class_t hid_class = hid_class_unknown;
  test_assert(hid_device_info(device, &name, &id, &type, &hid_class));
  test_assert(id == (((uint32_t)bank << 16) | (uint32_t)pin));
  test_assert(type == hid_type_gpio);

  // hid_device_userdata() is always the caller's own pointer...
  test_assert(hid_device_userdata(device) == &userdata_sentinel);

  // ...while hid_device_handle() exposes the backing hw_gpio_t*, same as
  // hid_register_wifi()'s hw_wifi_t* handle.
  hw_gpio_t *gpio = (hw_gpio_t *)hid_device_handle(device);
  test_assert(gpio != NULL);
  test_assert(hw_gpio_pin(gpio) == pin);
  test_assert(hw_gpio_bank(gpio) == bank);
  test_assert(hw_gpio_get_mode(gpio) == hw_gpio_input);

  test_assert(hid_deregister(instance, device));
  test_assert(!hid_device_info(device, NULL, NULL, NULL, NULL));

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}

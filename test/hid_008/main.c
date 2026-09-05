#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hid_register_wifi(): HID attaches to an already-initialized hw_wifi_t*
// (it does not create or own it - see hw/wifi.h's ownership split) and
// forwards every status update as a hid_event_type_wifi event. Skips
// cleanly if this platform/build has no real Wi-Fi client backend
// (hw_wifi_init_client() returns NULL) - see test/hw_016 for why.
#define HID_WIFI_TEST_SCAN_TIMEOUT_MS (15 * 1000)
#define HID_WIFI_TEST_POLL_MS 100

test_main_hw(0) {
  hw_wifi_t *wifi = hw_wifi_init_client(NULL);
  if (wifi == NULL) {
    sys_printf("[hid_008] no Wi-Fi client backend on this board\n");
    return;
  }

  sys_event_queue_t *queue = sys_event_queue_init(16);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  int userdata_sentinel = 0;
  hid_device_t *device = hid_register_wifi(instance, wifi, &userdata_sentinel);
  test_assert(device != NULL);

  const char *name = NULL;
  hid_type_t type = hid_type_none;
  test_assert(hid_device_info(device, &name, NULL, &type, NULL));
  test_assert_strequal(name, "wifi");
  test_assert(type == hid_type_wifi);

  // hid_device_userdata() is always the caller's own pointer...
  test_assert(hid_device_userdata(device) == &userdata_sentinel);

  // ...while hid_device_handle() is wifi again, same as hid_type_gpio's
  // hw_gpio_t*.
  test_assert(hid_device_handle(device) == wifi);

  sys_printf("[hid_008] scanning for nearby Wi-Fi networks...\n");
  test_assert(hw_wifi_scan(wifi));

  bool got_scan_complete = false;
  int scan_results = 0;
  uint64_t start = sys_timestamp_ms();
  while (!got_scan_complete &&
        sys_timestamp_ms() - start < HID_WIFI_TEST_SCAN_TIMEOUT_MS) {
    hw_poll();
    hid_event_t *event = (hid_event_t *)sys_event_queue_try_pop(queue);
    if (event != NULL) {
      test_assert(event->type == hid_event_type_wifi);
      test_assert(event->device == device);
      if (event->data.wifi.event == hw_wifi_event_scan) {
        if (event->data.wifi.network == NULL) {
          got_scan_complete = true;
        } else {
          scan_results++;
        }
      }
      hid_event_free(event);
    } else {
      sys_sleep_ms(HID_WIFI_TEST_POLL_MS);
    }
  }
  test_assert(got_scan_complete);
  sys_printf("[hid_008] scan complete: %d network(s) found\n", scan_results);

  test_assert(hid_deregister(instance, device));

  // hid_deregister() only detached the callback - wifi is still HID's
  // caller's to manage. If hid_deregister() had wrongly called
  // hw_wifi_deinit() itself, this would double-free/crash.
  hw_wifi_deinit(wifi);

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}

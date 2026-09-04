#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_wifi_init_accesspoint()/hw_wifi_deinit() lifecycle, and confirming
// hw_wifi_scan()/hw_wifi_connect()/hw_wifi_disconnect() are all correctly
// rejected on an AP handle, per hw/wifi.h's own documented contract.
// Pico-only, see hw_016/main.c (station init/deinit + scan) and
// hw_017/main.c (station connect/disconnect).
//
// hw_wifi_init_accesspoint() takes no callback - there's nothing to
// notify, see its own doc. Deliberately does not poll for or assert on
// any AP-side status either: hw/pico/wifi.c's _hw_wifi_poll() is an
// unconditional no-op for an AP handle (see its own comment) after a
// real-hardware investigation found that actually reading the AP's own
// link status there reliably deadlocks the next cyw43_arch_poll() call.
// Only a brief hw_poll() smoke check is done here, to confirm polling
// with an AP up doesn't crash.
#define HW_WIFI_TEST_AP_SSID "picofuse-ap-test"
#define HW_WIFI_TEST_AP_PASSWORD "picofuse123"

static void on_event(hw_wifi_t *wifi, hw_wifi_event_t event,
                     const hw_wifi_network_t *network, void *userdata) {
  (void)wifi;
  (void)event;
  (void)network;
  (void)userdata;
}

test_main_hw(0) {
  // NULL-safety: an invalid ssid is rejected.
  test_assert(hw_wifi_init_accesspoint(NULL, NULL, NULL,
                                       hw_wifi_auth_open) == NULL);

  hw_wifi_t *wifi = hw_wifi_init_accesspoint(
      NULL, HW_WIFI_TEST_AP_SSID, HW_WIFI_TEST_AP_PASSWORD,
      hw_wifi_auth_wpa2_aes);
  if (wifi == NULL) {
    sys_printf("[hw_018] no AP-capable Wi-Fi backend on this board\n");
    return;
  }
  sys_printf("[hw_018] AP \"%s\" up\n", HW_WIFI_TEST_AP_SSID);

  // A secured AP with no password is rejected.
  test_assert(hw_wifi_init_accesspoint(NULL, HW_WIFI_TEST_AP_SSID, NULL,
                                       hw_wifi_auth_wpa2_aes) == NULL);

  // The handle is a singleton - a second init while this one is still
  // active must fail, whichever entry point is used.
  test_assert(hw_wifi_init_client(NULL, on_event, NULL) == NULL);
  test_assert(hw_wifi_init_accesspoint(NULL, HW_WIFI_TEST_AP_SSID,
                                       HW_WIFI_TEST_AP_PASSWORD,
                                       hw_wifi_auth_wpa2_aes) == NULL);

  // Station-only operations must all be rejected on an AP handle.
  test_assert(hw_wifi_scan(wifi) == false);
  hw_wifi_network_t network = {0};
  test_assert(hw_wifi_connect(wifi, &network, "irrelevant") == false);
  test_assert(hw_wifi_disconnect(wifi) == false);

  // A brief poll smoke check - see the file comment on why this doesn't
  // assert on any particular event.
  for (int i = 0; i < 10; i++) {
    hw_poll();
    sys_sleep_ms(100);
  }

  hw_wifi_deinit(wifi);

  // The singleton is free again once deinited - a fresh init must succeed.
  hw_wifi_t *wifi2 = hw_wifi_init_accesspoint(
      NULL, HW_WIFI_TEST_AP_SSID, HW_WIFI_TEST_AP_PASSWORD,
      hw_wifi_auth_wpa2_aes);
  test_assert(wifi2 != NULL);
  hw_wifi_deinit(wifi2);
}

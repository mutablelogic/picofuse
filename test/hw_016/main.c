#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_wifi_init_client()/hw_wifi_deinit() lifecycle plus a real
// hw_wifi_scan(), gated to Pico for now - Darwin's CoreWLAN backend can't
// be exercised from a plain test binary (see hw/darwin/wifi.m's own doc:
// scanning needs Location Services, which requires a signed .app bundle),
// and Linux has no real hw_wifi_* backend yet.
#define HW_WIFI_TEST_SCAN_TIMEOUT_MS (15 * 1000)
#define HW_WIFI_TEST_POLL_MS 100

static int g_scan_results = 0;
static bool g_scan_done = false;

static void on_event(hw_wifi_t *wifi, hw_wifi_event_t event,
                     const hw_wifi_network_t *network, void *userdata) {
  (void)wifi;
  (void)userdata;
  if (event != hw_wifi_event_scan) {
    return;
  }
  if (network == NULL) {
    g_scan_done = true;
    return;
  }
  g_scan_results++;
  sys_debugf("hw_016", "scan result: ssid=\"%s\" channel=%u rssi=%d auth=0x%02x",
             network->ssid, network->channel, network->rssi, network->auth);
}

test_main_hw(0) {
  // NULL-safety: every operation must tolerate an invalid handle.
  test_assert(hw_wifi_scan(NULL) == false);
  test_assert(hw_wifi_connect(NULL, NULL, NULL) == false);
  test_assert(hw_wifi_disconnect(NULL) == false);
  hw_wifi_deinit(NULL); // must not crash

  hw_wifi_t *wifi = hw_wifi_init_client(NULL, on_event, NULL);
  if (wifi == NULL) {
    sys_printf("[hw_016] no Wi-Fi client backend on this board\n");
    return;
  }

  // hw_wifi_init_client() requires a non-NULL callback.
  test_assert(hw_wifi_init_client(NULL, NULL, NULL) == NULL);

  // The handle is a singleton - a second init while this one is still
  // active must fail, whichever entry point is used.
  test_assert(hw_wifi_init_client(NULL, on_event, NULL) == NULL);
  test_assert(hw_wifi_init_accesspoint(NULL, "hw_016-test", "irrelevant",
                                       hw_wifi_auth_wpa2_aes) == NULL);

  sys_printf("[hw_016] scanning for nearby Wi-Fi networks...\n");
  g_scan_results = 0;
  g_scan_done = false;
  test_assert(hw_wifi_scan(wifi));

  // A second scan while one is already in progress must be rejected.
  test_assert(hw_wifi_scan(wifi) == false);

  uint64_t start = sys_timestamp_ms();
  while (!g_scan_done &&
        sys_timestamp_ms() - start < HW_WIFI_TEST_SCAN_TIMEOUT_MS) {
    hw_poll();
    sys_sleep_ms(HW_WIFI_TEST_POLL_MS);
  }
  test_assert(g_scan_done);
  sys_printf("[hw_016] scan complete: %d network(s) found\n", g_scan_results);

  hw_wifi_deinit(wifi);

  // The singleton is free again once deinited - a fresh init must succeed.
  hw_wifi_t *wifi2 = hw_wifi_init_client(NULL, on_event, NULL);
  test_assert(wifi2 != NULL);
  hw_wifi_deinit(wifi2);
}

#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// hw_wifi_connect()/hw_wifi_disconnect() against a real Wi-Fi network -
// see test/CMakeLists.txt for how WIFI_SSID/WIFI_PASSWORD reach this file
// as compile definitions. Skips cleanly if WIFI_SSID is empty (the
// environment variable wasn't set when CMake configured this build).
// Scans for WIFI_SSID first rather than guessing its auth type, since
// hw_wifi_connect() needs the real hw_wifi_network_t a scan reports, not
// just a name/password pair. Pico-only for now, see hw_016/main.c.
#define HW_WIFI_TEST_TIMEOUT_MS (30 * 1000)
#define HW_WIFI_TEST_POLL_MS 100

static hw_wifi_network_t g_found_network;
static bool g_found = false;
static bool g_scan_done = false;
static volatile hw_wifi_event_t g_last_connect_event = 0;

static void on_event(hw_wifi_t *wifi, hw_wifi_event_t event,
                     const hw_wifi_network_t *network, void *userdata) {
  (void)wifi;
  (void)userdata;
  switch (event) {
  case hw_wifi_event_scan:
    if (network == NULL) {
      g_scan_done = true;
    } else if (strcmp(network->ssid, WIFI_SSID) == 0) {
      g_found_network = *network;
      g_found = true;
    }
    break;
  case hw_wifi_event_connected:
  case hw_wifi_event_disconnected:
  case hw_wifi_event_notfound:
  case hw_wifi_event_badauth:
  case hw_wifi_event_error:
    g_last_connect_event = event;
    break;
  default:
    break;
  }
}

static bool wait_for(volatile hw_wifi_event_t *slot, uint64_t timeout_ms) {
  uint64_t start = sys_timestamp_ms();
  while (*slot == 0 && sys_timestamp_ms() - start < timeout_ms) {
    hw_poll();
    sys_sleep_ms(HW_WIFI_TEST_POLL_MS);
  }
  return *slot != 0;
}

test_main_hw(0) {
  if (WIFI_SSID[0] == '\0') {
    sys_printf("[hw_017] no WIFI_SSID environment variable set, skipping\n");
    return;
  }

  hw_wifi_t *wifi = hw_wifi_init_client(NULL, on_event, NULL);
  if (wifi == NULL) {
    sys_printf("[hw_017] no Wi-Fi client backend on this board\n");
    return;
  }

  sys_printf("[hw_017] scanning for \"%s\"...\n", WIFI_SSID);
  test_assert(hw_wifi_scan(wifi));

  uint64_t start = sys_timestamp_ms();
  while (!g_scan_done &&
        sys_timestamp_ms() - start < HW_WIFI_TEST_TIMEOUT_MS) {
    hw_poll();
    sys_sleep_ms(HW_WIFI_TEST_POLL_MS);
  }
  test_assert(g_scan_done);
  test_assert(g_found);

  sys_printf("[hw_017] found \"%s\" auth=0x%02x channel=%u rssi=%d, "
             "connecting...\n",
             WIFI_SSID, g_found_network.auth, g_found_network.channel,
             g_found_network.rssi);
  g_last_connect_event = 0;
  test_assert(hw_wifi_connect(wifi, &g_found_network, WIFI_PASSWORD));
  test_assert(wait_for(&g_last_connect_event, HW_WIFI_TEST_TIMEOUT_MS));
  sys_printf("[hw_017] connect result: event=%d\n", (int)g_last_connect_event);
  test_assert(g_last_connect_event == hw_wifi_event_connected);

  sys_printf("[hw_017] disconnecting...\n");
  g_last_connect_event = 0;
  test_assert(hw_wifi_disconnect(wifi));
  test_assert(wait_for(&g_last_connect_event, HW_WIFI_TEST_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_disconnected);

  hw_wifi_deinit(wifi);
}

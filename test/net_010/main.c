#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// net_ntp_read() against a real NTP server, over a real Wi-Fi join - see
// test/CMakeLists.txt for how WIFI_SSID/WIFI_PASSWORD reach this file as
// compile definitions. Skips cleanly if WIFI_SSID is empty. Unlike
// net_005 (which assumes some network route already exists - true on
// host platforms, but not on Pico until something actually joins one),
// this does the join itself first - see net_007/main.c and hw_017/main.c
// for the same scan/connect scaffolding this mirrors. Pico-only - host
// platforms are already covered by net_005.
#define NET_010_WIFI_TIMEOUT_MS (30 * 1000)
#define NET_010_WIFI_POLL_MS 100
#define NET_010_NTP_TIMEOUT_MS 3000

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
    sys_sleep_ms(NET_010_WIFI_POLL_MS);
  }
  return *slot != 0;
}

test_main_hw(0) {
  if (WIFI_SSID[0] == '\0') {
    sys_printf("[net_010] no WIFI_SSID environment variable set, skipping\n");
    return;
  }

  hw_wifi_t *wifi = hw_wifi_init_client(NULL);
  if (wifi == NULL) {
    sys_printf("[net_010] no Wi-Fi client backend on this board\n");
    return;
  }
  hw_wifi_set_callback(wifi, on_event, NULL);

  sys_printf("[net_010] scanning for \"%s\"...\n", WIFI_SSID);
  test_assert(hw_wifi_scan(wifi));

  uint64_t start = sys_timestamp_ms();
  while (!g_scan_done &&
        sys_timestamp_ms() - start < NET_010_WIFI_TIMEOUT_MS) {
    hw_poll();
    sys_sleep_ms(NET_010_WIFI_POLL_MS);
  }
  test_assert(g_scan_done);
  test_assert(g_found);

  sys_printf("[net_010] found \"%s\", connecting...\n", WIFI_SSID);
  g_last_connect_event = 0;
  test_assert(hw_wifi_connect(wifi, &g_found_network, WIFI_PASSWORD));
  test_assert(wait_for(&g_last_connect_event, NET_010_WIFI_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_connected);

  // Wi-Fi is real and joined at this point - unlike net_005's own
  // "skip if no reply" hedge (which has to allow for no network route
  // existing at all), a missing NTP reply here still just means the test
  // network's firewall blocks outbound UDP/123, not that nothing's
  // plugged in - soft-skip rather than assert, same reasoning net_007
  // applies to its own IPv6 reachability check.
  net_ntp_t *ntp = net_ntp_init(NULL, 0, NET_010_NTP_TIMEOUT_MS);
  test_assert(ntp != NULL);

  sys_date_t date = {0};
  if (!net_ntp_read(ntp, &date)) {
    sys_printf("[net_010] no NTP reply - no route to time.cloudflare.com, "
              "skipping\n");
  } else {
    sys_printf("[net_010] date: seconds=%lld\n", (long long)date.seconds);
    test_assert(date.seconds > 1704067200); // after 2024-01-01
    test_assert(date.tzoffset == 0);
  }
  net_ntp_deinit(ntp);

  sys_printf("[net_010] disconnecting...\n");
  g_last_connect_event = 0;
  test_assert(hw_wifi_disconnect(wifi));
  test_assert(wait_for(&g_last_connect_event, NET_010_WIFI_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_disconnected);

  hw_wifi_deinit(wifi);
}

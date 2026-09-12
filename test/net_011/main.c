#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// net_ntp_register_hid() against a real NTP server, over a real Wi-Fi
// join - see net_010/main.c's own comment on why this variant of
// net_006 exists (Pico has no network route until something actually
// joins one), and test/CMakeLists.txt for how WIFI_SSID/WIFI_PASSWORD
// reach this file. Skips cleanly if WIFI_SSID is empty. Pico-only - host
// platforms are already covered by net_006.
#define NET_011_WIFI_TIMEOUT_MS (30 * 1000)
#define NET_011_WIFI_POLL_MS 100
#define NET_011_NTP_TIMEOUT_MS 3000
#define NET_011_POLL_INTERVAL_MS 100
#define NET_011_WAIT_MS (5 * 1000)
#define NET_011_POLL_MS 20

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
    sys_sleep_ms(NET_011_WIFI_POLL_MS);
  }
  return *slot != 0;
}

test_main_hw(0) {
  if (WIFI_SSID[0] == '\0') {
    sys_printf("[net_011] no WIFI_SSID environment variable set, skipping\n");
    return;
  }

  hw_wifi_t *wifi = hw_wifi_init_client(NULL);
  if (wifi == NULL) {
    sys_printf("[net_011] no Wi-Fi client backend on this board\n");
    return;
  }
  hw_wifi_set_callback(wifi, on_event, NULL);

  sys_printf("[net_011] scanning for \"%s\"...\n", WIFI_SSID);
  test_assert(hw_wifi_scan(wifi));

  uint64_t start = sys_timestamp_ms();
  while (!g_scan_done &&
        sys_timestamp_ms() - start < NET_011_WIFI_TIMEOUT_MS) {
    hw_poll();
    sys_sleep_ms(NET_011_WIFI_POLL_MS);
  }
  test_assert(g_scan_done);
  test_assert(g_found);

  sys_printf("[net_011] found \"%s\", connecting...\n", WIFI_SSID);
  g_last_connect_event = 0;
  test_assert(hw_wifi_connect(wifi, &g_found_network, WIFI_PASSWORD));
  test_assert(wait_for(&g_last_connect_event, NET_011_WIFI_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_connected);

  // Wi-Fi is real and joined - see net_010/main.c's own comment on why a
  // missing time event is still soft-skipped rather than asserted (an
  // NTP-specific network policy issue, not "nothing's plugged in").
  sys_event_queue_t *queue = sys_event_queue_init(8);
  test_assert(queue != NULL);
  hid_t *hid = hid_init(queue);
  test_assert(hid != NULL);

  net_ntp_t *ntp = net_ntp_init(NULL, 0, NET_011_NTP_TIMEOUT_MS);
  test_assert(ntp != NULL);

  hid_device_t *device =
      net_ntp_register_hid(hid, ntp, NET_011_POLL_INTERVAL_MS, (void *)0x1234);
  test_assert(device != NULL);
  test_assert(hid_device_userdata(device) == (void *)0x1234);

  bool got_time_event = false;
  uint64_t wait_start = sys_timestamp_ms();
  while (!got_time_event &&
        sys_timestamp_ms() - wait_start < NET_011_WAIT_MS) {
    hid_poll(hid);
    sys_event_t event = sys_event_queue_try_pop(queue);
    if (event != NULL) {
      hid_event_t *hid_event = (hid_event_t *)event;
      if (hid_event->type == hid_event_type_time) {
        sys_printf("[net_011] time event: seconds=%lld\n",
                   (long long)hid_event->data.time.date.seconds);
        test_assert(hid_event->data.time.date.seconds > 1704067200);
        test_assert(hid_event->data.time.date.tzoffset == 0);
        got_time_event = true;
      }
      hid_event_free(hid_event);
    }
    sys_sleep_ms(NET_011_POLL_MS);
  }

  if (!got_time_event) {
    sys_printf("[net_011] no time event - no route to time.cloudflare.com, "
              "skipping\n");
  }

  hid_deregister(hid, device);
  net_ntp_deinit(ntp);
  hid_deinit(hid);
  sys_event_queue_deinit(queue);

  sys_printf("[net_011] disconnecting...\n");
  g_last_connect_event = 0;
  test_assert(hw_wifi_disconnect(wifi));
  test_assert(wait_for(&g_last_connect_event, NET_011_WIFI_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_disconnected);

  hw_wifi_deinit(wifi);
}

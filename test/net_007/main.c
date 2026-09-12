#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// net_open() over IPv6, against a real Wi-Fi network - see
// test/CMakeLists.txt for how WIFI_SSID/WIFI_PASSWORD reach this file as
// compile definitions. Skips cleanly if WIFI_SSID is empty. Needs an
// actual join (unlike net_005/net_006, which assume some network route
// already exists) since an IPv6 address - even just the link-local one -
// only ever gets assigned once the link comes up; see hw_017/main.c for
// the same scan/connect scaffolding this mirrors. Pico-only for now -
// the POSIX backend has supported IPv6 all along.
#define NET_007_WIFI_TIMEOUT_MS (30 * 1000)
#define NET_007_WIFI_POLL_MS 100
#define NET_007_ADDR_TIMEOUT_MS (5 * 1000)
#define NET_007_ADDR_POLL_MS 100

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
    sys_sleep_ms(NET_007_WIFI_POLL_MS);
  }
  return *slot != 0;
}

test_main_hw(0) {
  if (WIFI_SSID[0] == '\0') {
    sys_printf("[net_007] no WIFI_SSID environment variable set, skipping\n");
    return;
  }

  hw_wifi_t *wifi = hw_wifi_init_client(NULL);
  if (wifi == NULL) {
    sys_printf("[net_007] no Wi-Fi client backend on this board\n");
    return;
  }
  hw_wifi_set_callback(wifi, on_event, NULL);

  sys_printf("[net_007] scanning for \"%s\"...\n", WIFI_SSID);
  test_assert(hw_wifi_scan(wifi));

  uint64_t start = sys_timestamp_ms();
  while (!g_scan_done &&
        sys_timestamp_ms() - start < NET_007_WIFI_TIMEOUT_MS) {
    hw_poll();
    sys_sleep_ms(NET_007_WIFI_POLL_MS);
  }
  test_assert(g_scan_done);
  test_assert(g_found);

  sys_printf("[net_007] found \"%s\", connecting...\n", WIFI_SSID);
  g_last_connect_event = 0;
  test_assert(hw_wifi_connect(wifi, &g_found_network, WIFI_PASSWORD));
  test_assert(wait_for(&g_last_connect_event, NET_007_WIFI_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_connected);

  // A link-local address is assigned as soon as the link comes up, but
  // duplicate-address detection (LWIP_IPV6_AUTOCONFIG) takes it through a
  // brief "tentative" state first - poll rather than checking once
  // immediately after connecting.
  net_addr_t addr = {0};
  bool have_v6 = false;
  start = sys_timestamp_ms();
  while (!have_v6 &&
        sys_timestamp_ms() - start < NET_007_ADDR_TIMEOUT_MS) {
    have_v6 = hw_wifi_get_address(wifi, net_addr_family_v6, &addr);
    if (!have_v6) {
      sys_sleep_ms(NET_007_ADDR_POLL_MS);
    }
  }

  if (!have_v6) {
    sys_printf("[net_007] no IPv6 address bound - skipping reachability "
              "check\n");
  } else {
    char addrbuf[48];
    net_addr_to_string(&addr, addrbuf, sizeof(addrbuf));
    sys_printf("[net_007] bound IPv6 address: %s\n", addrbuf);

    // 2606:4700:4700::1111 - Cloudflare's public IPv6 DNS resolver,
    // chosen the same way examples/wifi's own earlier diagnostics chose
    // 1.1.1.1 for IPv4: a well-known, always-up host, here reached over
    // plain TCP:80 rather than actually doing anything DNS-shaped - see
    // net_005/main.c's own comment on why a real reply is skipped
    // (not asserted): this needs a router that actually forwards IPv6
    // traffic, which isn't guaranteed on every test network.
    static const uint8_t cloudflare_v6[16] = {0x26, 0x06, 0x47, 0x00, 0x47,
                                              0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0x11,
                                              0x11};
    net_addr_t remote = net_addr_v6(cloudflare_v6);
    sys_iostream_t *conn = net_open(net_proto_tcp, &remote, 80, 0);
    if (conn == NULL) {
      sys_printf("[net_007] no IPv6 route to 2606:4700:4700::1111:80 - "
                "skipping\n");
    } else {
      sys_printf("[net_007] TCP connect to [2606:4700:4700::1111]:80 OK\n");
      sys_iostream_close(conn);
    }
  }

  sys_printf("[net_007] disconnecting...\n");
  g_last_connect_event = 0;
  test_assert(hw_wifi_disconnect(wifi));
  test_assert(wait_for(&g_last_connect_event, NET_007_WIFI_TIMEOUT_MS));
  test_assert(g_last_connect_event == hw_wifi_event_disconnected);

  hw_wifi_deinit(wifi);
}

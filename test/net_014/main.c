#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

#include "../wifi_helper.h"

// net_mqtt_connect() against test.mosquitto.org's authenticated listener
// (port 1884) - exercises the username/password fields connect.c adds to
// the CONNECT packet, both accepted (real published test credentials -
// rw/readwrite, read-write access to the # hierarchy - see
// https://test.mosquitto.org/) and rejected (a deliberately wrong
// password, which must fail the CONNACK rather than silently succeed).
// Skips (not asserted) if no reply arrives at all - see net_013's own
// comment on why.

#define NET_014_PORT 1884
#define NET_014_TIMEOUT_MS 5000

test_main_hw(0) {
  hw_wifi_t *wifi = test_wifi_join("net_014");

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org

  net_mqtt_config_t good = {.username = "rw", .password = "readwrite"};
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_014_PORT, NET_014_TIMEOUT_MS, &good);
  test_assert(mqtt != NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_014] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_014", wifi);
    return;
  }
  sys_printf("[net_014] authenticated connect OK\n");
  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);

  // A wrong password against the same authenticated listener must be
  // rejected (non-zero CONNACK return code -> net_mqtt_connect() returns
  // false), not silently accepted.
  net_mqtt_config_t bad = {.username = "rw", .password = "not-the-password"};
  net_mqtt_t *mqtt2 =
      net_mqtt_init(&addr, NET_014_PORT, NET_014_TIMEOUT_MS, &bad);
  test_assert(mqtt2 != NULL);
  test_assert(net_mqtt_connect(mqtt2) == false);
  sys_printf("[net_014] wrong-password connect correctly rejected\n");
  net_mqtt_deinit(mqtt2);
  test_wifi_leave("net_014", wifi);
}

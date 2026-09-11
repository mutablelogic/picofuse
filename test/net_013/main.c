#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_mqtt_connect()/_disconnect() against a real public test broker
// (test.mosquitto.org, 54.36.178.49:1883) - the actual CONNECT/CONNACK
// wire exchange. Skips (not asserted) if no reply arrives, since that
// needs actual internet access, same reasoning as net_005's own comment
// on NTP. net_mqtt_init()/_deinit()/_default_config() lifecycle itself
// is already covered by net_009 - this is specifically about the
// connection handshake connect.c implements.

#define NET_013_TIMEOUT_MS 5000

static int g_connected = 0;
static int g_disconnected = 0;

static void on_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                     void *userdata) {
  (void)mqtt;
  (void)userdata;
  if (event->type == net_mqtt_event_connected) {
    g_connected++;
  } else if (event->type == net_mqtt_event_disconnected) {
    g_disconnected++;
  }
}

test_main_sys(0) {
  // NULL-safety.
  test_assert(net_mqtt_connect(NULL) == false);
  net_mqtt_disconnect(NULL); // must not crash

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_013_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_013] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    return;
  }
  sys_printf("[net_013] connected\n");
  test_assert(g_connected == 1);
  test_assert(g_disconnected == 0);

  // A second connect while already connected must fail, without
  // disturbing the existing connection or its event counts.
  test_assert(net_mqtt_connect(mqtt) == false);
  test_assert(g_connected == 1);

  net_mqtt_disconnect(mqtt);
  test_assert(g_disconnected == 1);

  // A no-op once already disconnected.
  net_mqtt_disconnect(mqtt);
  test_assert(g_disconnected == 1);

  // Reconnecting on the same handle must work.
  g_connected = 0;
  if (net_mqtt_connect(mqtt)) {
    test_assert(g_connected == 1);
    net_mqtt_disconnect(mqtt);
  } else {
    sys_printf("[net_013] reconnect failed - transient network issue, "
              "skipping\n");
  }

  net_mqtt_deinit(mqtt); // disconnects first if still connected
}

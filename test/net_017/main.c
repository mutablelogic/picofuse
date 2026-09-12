#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#include "../wifi_helper.h"

// net_mqtt_publish() QoS 1 against a real broker (test.mosquitto.org) -
// the actual PUBLISH/PUBACK round trip, unlike QoS 0 (net_015), which
// never waits for a reply at all. Skips (not asserted) if no reply
// arrives, same reasoning as net_013's own comment.

#define NET_017_TIMEOUT_MS 5000
#define NET_017_WAIT_MS (5 * 1000)
#define NET_017_POLL_MS 20

static int g_sent_events = 0;
static int g_error_events = 0;
static uint32_t g_last_sent_message_id = 0;

static void on_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                     void *userdata) {
  (void)mqtt;
  (void)userdata;
  if (event->type == net_mqtt_event_sent) {
    g_sent_events++;
    g_last_sent_message_id = event->data.sent.message_id;
  } else if (event->type == net_mqtt_event_error) {
    g_error_events++;
    sys_printf("[net_017] error event: %s (message_id=%u)\n",
              event->data.error.message, (unsigned)event->data.error.message_id);
  } else if (event->type == net_mqtt_event_disconnected) {
    sys_printf("[net_017] disconnected event\n");
  }
}

static bool wait_for_sent(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_017_WAIT_MS) {
    net_poll();
    if (g_last_sent_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_017_POLL_MS);
  }
  return false;
}

test_main_hw(0) {
  hw_wifi_t *wifi = test_wifi_join("net_017");

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_017_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_017] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_017", wifi);
    return;
  }

  // A QoS 1 publish isn't "sent" (net_mqtt_event_sent) until a matching
  // PUBACK actually comes back - unlike QoS 0, staging it alone isn't
  // enough even once net_poll() has written it; wait_for_sent() here is
  // polling through both the write and the PUBACK read.
  uint32_t id1 = net_mqtt_publish(mqtt, "picofuse/test/net_017", NULL, 0,
                                  net_mqtt_qos_1, false);
  test_assert(id1 != 0);
  test_assert(wait_for_sent(id1));
  test_assert(g_sent_events == 1);
  test_assert(g_error_events == 0);
  sys_printf("[net_017] QoS 1 publish acked, id=%u\n", (unsigned)id1);

  // The slot only frees once the PUBACK lands - a second publish
  // immediately after the first should need no more than one extra
  // net_poll() round (the PUBACK for id1 already arrived above) to
  // succeed, not block.
  const char *msg = "hello from net_017";
  uint32_t id2 = net_mqtt_publish(mqtt, "picofuse/test/net_017", msg,
                                  strlen(msg), net_mqtt_qos_1, true);
  test_assert(id2 != 0);
  test_assert(id2 > id1);
  test_assert(wait_for_sent(id2));
  test_assert(g_sent_events == 2);

  // Clear the retained message we just left behind - an empty retained
  // publish removes it (see net_mqtt_publish()'s own doc on retain).
  uint32_t id3 = net_mqtt_publish(mqtt, "picofuse/test/net_017", NULL, 0,
                                  net_mqtt_qos_1, true);
  test_assert(id3 != 0);
  test_assert(wait_for_sent(id3));
  test_assert(g_sent_events == 3);

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_017] QoS 1 round trip OK, connection stayed healthy\n");
  test_wifi_leave("net_017", wifi);
}

#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// net_mqtt_publish() QoS 2 against a real broker (test.mosquitto.org) -
// the actual PUBLISH/PUBREC/PUBREL/PUBCOMP round trip, one leg further
// than QoS 1 (net_017). Skips (not asserted) if no reply arrives, same
// reasoning as net_013's own comment.

#define NET_018_TIMEOUT_MS 5000
#define NET_018_WAIT_MS (5 * 1000)
#define NET_018_POLL_MS 20

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
    sys_printf("[net_018] error event: %s (message_id=%u)\n",
              event->data.error.message, (unsigned)event->data.error.message_id);
  } else if (event->type == net_mqtt_event_disconnected) {
    sys_printf("[net_018] disconnected event\n");
  }
}

static bool wait_for_sent(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_018_WAIT_MS) {
    net_poll();
    if (g_last_sent_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_018_POLL_MS);
  }
  return false;
}

test_main_sys(0) {
  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_018_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_018] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    return;
  }

  // A QoS 2 publish isn't "sent" until the full PUBREC/PUBREL/PUBCOMP
  // chain completes - wait_for_sent() here is polling through the
  // write, the PUBREC read (and our own PUBREL write it triggers), and
  // finally the PUBCOMP read.
  uint32_t id1 = net_mqtt_publish(mqtt, "picofuse/test/net_018", NULL, 0,
                                  net_mqtt_qos_2, false);
  test_assert(id1 != 0);
  test_assert(wait_for_sent(id1));
  test_assert(g_sent_events == 1);
  test_assert(g_error_events == 0);
  sys_printf("[net_018] QoS 2 publish acked, id=%u\n", (unsigned)id1);

  // The slot only frees once PUBCOMP lands - a second publish right
  // after the first should need no more than a couple of extra
  // net_poll() rounds, not block.
  const char *msg = "hello from net_018";
  uint32_t id2 = net_mqtt_publish(mqtt, "picofuse/test/net_018", msg,
                                  strlen(msg), net_mqtt_qos_2, true);
  test_assert(id2 != 0);
  test_assert(id2 > id1);
  test_assert(wait_for_sent(id2));
  test_assert(g_sent_events == 2);

  // Clear the retained message we just left behind.
  uint32_t id3 = net_mqtt_publish(mqtt, "picofuse/test/net_018", NULL, 0,
                                  net_mqtt_qos_2, true);
  test_assert(id3 != 0);
  test_assert(wait_for_sent(id3));
  test_assert(g_sent_events == 3);

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_018] QoS 2 round trip OK, connection stayed healthy\n");
}

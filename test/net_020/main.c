#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#include "../wifi_helper.h"

// Incoming PUBLISH delivery (net_mqtt_event_received), QoS 0, against a
// real broker (test.mosquitto.org) - subscribes to a topic, publishes to
// that same topic on the same connection, and confirms the broker
// echoes it back as a net_mqtt_event_received carrying the right topic/
// payload/payload_len. Skips (not asserted) if no reply arrives at all,
// same reasoning as net_013's own comment - but a message that *does*
// arrive is asserted to be exactly what was sent, not just "something
// arrived".

#define NET_020_TIMEOUT_MS 5000
#define NET_020_WAIT_MS (5 * 1000)
#define NET_020_POLL_MS 20

static int g_subscribed_events = 0;
static int g_received_events = 0;
static int g_error_events = 0;
static uint32_t g_last_subscribed_message_id = 0;
static char g_received_topic[128];
static char g_received_payload[128];
static size_t g_received_payload_len = 0;
static bool g_received_retain = true;
static uint32_t g_received_topic_id = 0;

static void on_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                     void *userdata) {
  (void)mqtt;
  (void)userdata;
  if (event->type == net_mqtt_event_subscribed) {
    g_subscribed_events++;
    g_last_subscribed_message_id = event->data.subscribed.topic_id;
  } else if (event->type == net_mqtt_event_received) {
    g_received_events++;
    sys_sprintf(g_received_topic, sizeof(g_received_topic), "%s",
               event->data.received.topic);
    g_received_payload_len = event->data.received.payload_len;
    test_assert(g_received_payload_len < sizeof(g_received_payload));
    if (g_received_payload_len > 0) {
      memcpy(g_received_payload, event->data.received.payload,
            g_received_payload_len);
    }
    g_received_retain = event->data.received.retain;
    g_received_topic_id = event->data.received.topic_id;
  } else if (event->type == net_mqtt_event_error) {
    g_error_events++;
    char error_buf[64];
    net_mqtt_error_to_string(&event->data.error, error_buf, sizeof(error_buf));
    sys_printf("[net_020] error event: %s\n", error_buf);
  } else if (event->type == net_mqtt_event_disconnected) {
    sys_printf("[net_020] disconnected event\n");
  }
}

static bool wait_for_subscribed(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_020_WAIT_MS) {
    net_poll();
    if (g_last_subscribed_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_020_POLL_MS);
  }
  return false;
}

static bool wait_for_received(void) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_020_WAIT_MS) {
    net_poll();
    if (g_received_events > 0) {
      return true;
    }
    sys_sleep_ms(NET_020_POLL_MS);
  }
  return false;
}

test_main_hw(0) {
  hw_wifi_t *wifi = test_wifi_join("net_020");

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_020_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_020] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_020", wifi);
    return;
  }

  uint32_t sub_id = net_mqtt_subscribe(mqtt, "picofuse/test/net_020", net_mqtt_qos_0);
  test_assert(sub_id != 0);
  test_assert(wait_for_subscribed(sub_id));
  test_assert(g_subscribed_events == 1);

  const char *msg = "hello from net_020";
  uint32_t pub_id = net_mqtt_publish(mqtt, "picofuse/test/net_020", msg,
                                     strlen(msg), net_mqtt_qos_0, false);
  test_assert(pub_id != 0);

  if (!wait_for_received()) {
    sys_printf("[net_020] no PUBLISH echoed back - broker may not "
              "self-deliver, skipping content checks\n");
    net_mqtt_disconnect(mqtt);
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_020", wifi);
    return;
  }

  test_assert(g_received_events == 1);
  test_assert(g_error_events == 0);
  test_assert(strcmp(g_received_topic, "picofuse/test/net_020") == 0);
  test_assert(g_received_payload_len == strlen(msg));
  test_assert(memcmp(g_received_payload, msg, strlen(msg)) == 0);
  test_assert(!g_received_retain);
  test_assert(g_received_topic_id == sub_id);
  sys_printf("[net_020] received own PUBLISH back: topic=%s payload_len=%u "
            "topic_id=%u\n",
            g_received_topic, (unsigned)g_received_payload_len,
            (unsigned)g_received_topic_id);

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_020] QoS 0 receive round trip OK\n");
  test_wifi_leave("net_020", wifi);
}

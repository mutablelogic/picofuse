#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#include "../wifi_helper.h"

// Incoming PUBLISH delivery (net_mqtt_event_received), QoS 1, against a
// real broker (test.mosquitto.org) - subscribes at QoS 1, publishes to
// that same topic at QoS 1 on the same connection, and confirms the
// content arrives correctly. A malformed or missing PUBACK for the
// incoming message would typically get the broker to eventually
// redeliver rather than something this short a test could directly
// observe, so the closest thing to a positive check on that is a clean
// net_poll() loop afterward with no error/disconnect. Skips (not
// asserted) if no reply arrives at all, same reasoning as net_013's own
// comment.

#define NET_025_TIMEOUT_MS 5000
#define NET_025_WAIT_MS (5 * 1000)
#define NET_025_POLL_MS 20

static int g_subscribed_events = 0;
static int g_sent_events = 0;
static int g_received_events = 0;
static int g_error_events = 0;
static uint32_t g_last_subscribed_message_id = 0;
static uint32_t g_last_sent_message_id = 0;
static net_mqtt_qos_t g_last_granted_qos = net_mqtt_qos_2;
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
    g_last_granted_qos = event->data.subscribed.granted_qos;
  } else if (event->type == net_mqtt_event_sent) {
    g_sent_events++;
    g_last_sent_message_id = event->data.sent.message_id;
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
    sys_printf("[net_025] error event: %s\n", error_buf);
  } else if (event->type == net_mqtt_event_disconnected) {
    sys_printf("[net_025] disconnected event\n");
  }
}

static bool wait_for_subscribed(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_025_WAIT_MS) {
    net_poll();
    if (g_last_subscribed_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_025_POLL_MS);
  }
  return false;
}

// Waits for both this publish's own PUBACK (net_mqtt_event_sent) and the
// broker echoing it back as a net_mqtt_event_received - the two aren't
// guaranteed to arrive in any particular order, so this just polls until
// both have happened.
static bool wait_for_sent_and_received(uint32_t sent_id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_025_WAIT_MS) {
    net_poll();
    if (g_last_sent_message_id == sent_id && g_received_events > 0) {
      return true;
    }
    sys_sleep_ms(NET_025_POLL_MS);
  }
  return false;
}

test_main_hw(0) {
  hw_wifi_t *wifi = test_wifi_join("net_025");

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_025_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_025] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_025", wifi);
    return;
  }

  uint32_t sub_id =
      net_mqtt_subscribe(mqtt, "picofuse/test/net_025", net_mqtt_qos_1);
  test_assert(sub_id != 0);
  test_assert(wait_for_subscribed(sub_id));
  test_assert(g_subscribed_events == 1);
  test_assert(g_last_granted_qos == net_mqtt_qos_1);
  sys_printf("[net_025] SUBACK granted qos=%d\n", (int)g_last_granted_qos);

  const char *msg = "hello from net_025";
  uint32_t pub_id = net_mqtt_publish(mqtt, "picofuse/test/net_025", msg,
                                     strlen(msg), net_mqtt_qos_1, false);
  test_assert(pub_id != 0);

  if (!wait_for_sent_and_received(pub_id)) {
    sys_printf("[net_025] no PUBLISH echoed back (or own PUBACK never "
              "arrived) - broker may not self-deliver, skipping content "
              "checks\n");
    net_mqtt_disconnect(mqtt);
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_025", wifi);
    return;
  }

  test_assert(g_sent_events == 1);
  test_assert(g_received_events == 1);
  test_assert(g_error_events == 0);
  test_assert(strcmp(g_received_topic, "picofuse/test/net_025") == 0);
  test_assert(g_received_payload_len == strlen(msg));
  test_assert(memcmp(g_received_payload, msg, strlen(msg)) == 0);
  test_assert(!g_received_retain);
  test_assert(g_received_topic_id == sub_id);
  sys_printf("[net_025] received own PUBLISH back: topic=%s payload_len=%u "
            "topic_id=%u\n",
            g_received_topic, (unsigned)g_received_payload_len,
            (unsigned)g_received_topic_id);

  // The connection must still be healthy after our own PUBACK for the
  // incoming message - see this file's own comment on why a stronger
  // check isn't practical here.
  for (int i = 0; i < 10; i++) {
    net_poll();
    sys_sleep_ms(NET_025_POLL_MS);
  }
  test_assert(g_error_events == 0);

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_025] QoS 1 receive round trip OK\n");
  test_wifi_leave("net_025", wifi);
}

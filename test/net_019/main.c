#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

#include "../wifi_helper.h"

// net_mqtt_subscribe()/net_mqtt_unsubscribe() QoS 0 against a real
// broker (test.mosquitto.org) - the actual SUBSCRIBE/SUBACK and
// UNSUBSCRIBE/UNSUBACK round trips. Skips (not asserted) if no reply
// arrives, same reasoning as net_013's own comment.

#define NET_019_TIMEOUT_MS 5000
#define NET_019_WAIT_MS (5 * 1000)
#define NET_019_POLL_MS 20

static int g_subscribed_events = 0;
static int g_unsubscribed_events = 0;
static int g_error_events = 0;
static uint32_t g_last_subscribed_message_id = 0;
static uint32_t g_last_unsubscribed_message_id = 0;
static net_mqtt_qos_t g_last_granted_qos = net_mqtt_qos_2;

static void on_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                     void *userdata) {
  (void)mqtt;
  (void)userdata;
  if (event->type == net_mqtt_event_subscribed) {
    g_subscribed_events++;
    g_last_subscribed_message_id = event->data.subscribed.message_id;
    g_last_granted_qos = event->data.subscribed.granted_qos;
  } else if (event->type == net_mqtt_event_unsubscribed) {
    g_unsubscribed_events++;
    g_last_unsubscribed_message_id = event->data.unsubscribed.message_id;
  } else if (event->type == net_mqtt_event_error) {
    g_error_events++;
    sys_printf("[net_019] error event: %s (message_id=%u)\n",
              event->data.error.message, (unsigned)event->data.error.message_id);
  } else if (event->type == net_mqtt_event_disconnected) {
    sys_printf("[net_019] disconnected event\n");
  }
}

static bool wait_for_subscribed(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_019_WAIT_MS) {
    net_poll();
    if (g_last_subscribed_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_019_POLL_MS);
  }
  return false;
}

static bool wait_for_unsubscribed(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_019_WAIT_MS) {
    net_poll();
    if (g_last_unsubscribed_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_019_POLL_MS);
  }
  return false;
}

test_main_hw(0) {
  hw_wifi_t *wifi = test_wifi_join("net_019");

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_019_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_019] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    test_wifi_leave("net_019", wifi);
    return;
  }

  // Not yet subscribed - unsubscribing from a filter this handle never
  // subscribed to fails immediately, no wire traffic at all.
  test_assert(net_mqtt_unsubscribe(mqtt, "picofuse/test/net_019") == 0);

  uint32_t sub_id = net_mqtt_subscribe(mqtt, "picofuse/test/net_019", net_mqtt_qos_0);
  test_assert(sub_id != 0);
  test_assert(wait_for_subscribed(sub_id));
  test_assert(g_subscribed_events == 1);
  test_assert(g_error_events == 0);
  test_assert(g_last_granted_qos == net_mqtt_qos_0);
  sys_printf("[net_019] SUBACK granted qos=%d, id=%u\n", (int)g_last_granted_qos,
            (unsigned)sub_id);

  // Unsubscribing from that same filter is a second, independent
  // request, on its own separate pending slot - not blocked by the
  // subscribe above, since that one already completed and freed its own
  // slot regardless.
  uint32_t unsub_id = net_mqtt_unsubscribe(mqtt, "picofuse/test/net_019");
  test_assert(unsub_id != 0);
  test_assert(unsub_id > sub_id);
  test_assert(wait_for_unsubscribed(unsub_id));
  test_assert(g_unsubscribed_events == 1);
  test_assert(g_error_events == 0);
  sys_printf("[net_019] UNSUBACK received, id=%u\n", (unsigned)unsub_id);

  // Already unsubscribed - a second unsubscribe for the same filter
  // fails immediately, same as the very first check above.
  test_assert(net_mqtt_unsubscribe(mqtt, "picofuse/test/net_019") == 0);

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_019] subscribe/unsubscribe round trip OK\n");
  test_wifi_leave("net_019", wifi);
}

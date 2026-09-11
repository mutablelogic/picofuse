#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// net_mqtt_publish() QoS 0 against a real broker (test.mosquitto.org) -
// skips (not asserted) if no reply arrives at all, since that needs
// actual internet access, same reasoning as net_013's own comment.
// QoS 1 has its own dedicated test (net_017, the PUBACK round trip) -
// this file only confirms QoS 2 (still unimplemented) is rejected.
// There's no net_mqtt_subscribe() yet to read a publish back with, so
// this can only verify the send itself succeeds (and the connection
// survives it, rather than the broker dropping a malformed packet) -
// full round-trip delivery is for whenever subscribe exists.
//
// net_mqtt_publish() only stages the message - net_poll() does the
// actual send on a later call (see its own doc), so every publish here
// is followed by polling until the matching net_mqtt_event_sent arrives.

#define NET_015_TIMEOUT_MS 5000
#define NET_015_WAIT_MS (5 * 1000)
#define NET_015_POLL_MS 20

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
  }
}

// Polls until a net_mqtt_event_sent for id has arrived (g_last_sent_message_id
// == id) or NET_015_WAIT_MS elapses.
static bool wait_for_sent(uint32_t id) {
  uint64_t start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < NET_015_WAIT_MS) {
    net_poll();
    if (g_last_sent_message_id == id) {
      return true;
    }
    sys_sleep_ms(NET_015_POLL_MS);
  }
  return false;
}

test_main_sys(0) {
  // NULL-safety.
  test_assert(net_mqtt_publish(NULL, "x", NULL, 0, net_mqtt_qos_0, false) ==
             0);

  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  net_mqtt_t *mqtt =
      net_mqtt_init(&addr, NET_MQTT_PORT, NET_015_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);

  // Publishing before connecting must fail.
  test_assert(net_mqtt_publish(mqtt, "picofuse/test/net_015", NULL, 0,
                               net_mqtt_qos_0, false) == 0);

  if (!net_mqtt_connect(mqtt)) {
    sys_printf("[net_015] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(mqtt);
    return;
  }

  // QoS 2 isn't implemented yet - must fail cleanly (nothing staged)
  // rather than crash or silently downgrade to QoS 0/1.
  test_assert(net_mqtt_publish(mqtt, "picofuse/test/net_015", NULL, 0,
                               net_mqtt_qos_2, false) == 0);
  test_assert(g_sent_events == 0);

  // A real QoS 0 publish with no payload - staged (a non-zero id back
  // immediately), then actually sent once net_poll() runs. The id in the
  // net_mqtt_event_sent callback must exactly match what was returned,
  // since that's the whole point of returning one: correlating this
  // specific call with its own completion event.
  uint32_t id1 = net_mqtt_publish(mqtt, "picofuse/test/net_015", NULL, 0,
                                  net_mqtt_qos_0, false);
  test_assert(id1 != 0);
  test_assert(g_sent_events == 0); // not sent yet - only staged so far

  // A second publish while the first is still staged/pending blocks
  // (only one outstanding publish at a time for now - see
  // net_mqtt_publish()'s own doc) rather than failing immediately. This
  // one call is nothing draining it, so it blocks for the full handle
  // timeout and then gives up (0) - the real "another thread/core drains
  // it and this wakes up successfully" case is net_016's own job to
  // verify, with the concurrency this single-threaded test doesn't have.
  test_assert(net_mqtt_publish(mqtt, "picofuse/test/net_015", NULL, 0,
                               net_mqtt_qos_0, false) == 0);

  test_assert(wait_for_sent(id1));
  test_assert(g_sent_events == 1);
  test_assert(g_error_events == 0);

  // And with a real payload - the id must be different from (and, given
  // the counter only ever increases, greater than) the previous one. Now
  // that the first publish has drained, this one must succeed.
  const char *msg = "hello from net_015";
  uint32_t id2 = net_mqtt_publish(mqtt, "picofuse/test/net_015", msg,
                                  strlen(msg), net_mqtt_qos_0, false);
  test_assert(id2 != 0);
  test_assert(id2 > id1);
  test_assert(wait_for_sent(id2));
  test_assert(g_sent_events == 2);

  // The connection must still be healthy afterward - a malformed PUBLISH
  // would typically get a real broker to drop it instead.
  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_015] published OK, connection stayed healthy\n");
}

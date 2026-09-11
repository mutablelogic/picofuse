#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// Regression test: a PUBACK arriving *after* this client already gave up
// on it locally (net_mqtt_publish()'s own timeout_ms elapsed) used to
// trip a sys_assert() in poll.c's read handlers and panic the whole
// process - ordinary network latency against a real broker was enough
// to trigger it, not a hypothetical. Reproduces it deterministically
// with a small local fake broker (loopback listener) that we fully
// control the timing of, rather than relying on a real broker's own
// latency to ever actually race our timeout - see net_008's own comment
// on this loopback pattern. Skips gracefully wherever there's no real
// backend yet - see net_002's own comment.

#define NET_022_PORT 17822
#define NET_022_MQTT_TIMEOUT_MS 500
#define NET_022_LATE_PUBACK_DELAY_MS 1200
#define NET_022_WAIT_MS (5 * 1000)
#define NET_022_POLL_MS 20

static int g_sent_events = 0;
static int g_error_events = 0;
static int g_disconnected_events = 0;
static uint32_t g_last_error_message_id = 0;
static sys_atomic_t g_broker_done;

static void on_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                     void *userdata) {
  (void)mqtt;
  (void)userdata;
  if (event->type == net_mqtt_event_sent) {
    g_sent_events++;
  } else if (event->type == net_mqtt_event_error) {
    g_error_events++;
    g_last_error_message_id = event->data.error.message_id;
  } else if (event->type == net_mqtt_event_disconnected) {
    g_disconnected_events++;
  }
}

// Reads one MQTT fixed header + Remaining Length varint from conn,
// busy-polling until each byte is available. Returns the type byte and
// the number of bytes remaining for the rest of the packet.
static uint8_t read_fixed_header(sys_iostream_t *conn, uint32_t *out_remaining) {
  uint8_t type_byte = 0;
  while (sys_iostream_read(conn, (char *)&type_byte, 1) == 0) {
    sys_sleep_ms(NET_022_POLL_MS);
  }
  uint32_t value = 0, multiplier = 1;
  for (;;) {
    uint8_t b = 0;
    while (sys_iostream_read(conn, (char *)&b, 1) == 0) {
      sys_sleep_ms(NET_022_POLL_MS);
    }
    value += (uint32_t)(b & 0x7F) * multiplier;
    if ((b & 0x80) == 0) {
      break;
    }
    multiplier *= 128;
  }
  *out_remaining = value;
  return type_byte;
}

static void read_exact(sys_iostream_t *conn, char *buf, size_t n) {
  size_t got = 0;
  while (got < n) {
    got += sys_iostream_read(conn, buf + got, n - got);
    if (got < n) {
      sys_sleep_ms(NET_022_POLL_MS);
    }
  }
}

static void write_exact(sys_iostream_t *conn, const uint8_t *buf, size_t n) {
  size_t wrote = 0;
  while (wrote < n) {
    wrote += sys_iostream_write(conn, (const char *)buf + wrote, n - wrote);
  }
}

// Plays just enough of a broker to get net_mqtt_connect() to succeed and
// one QoS 1 net_mqtt_publish() written - then deliberately waits until
// well after the client's own timeout_ms has elapsed before finally
// sending the PUBACK.
static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
                      const net_addr_t *remote, uint16_t remote_port,
                      void *userdata) {
  (void)listener;
  (void)userdata;
  (void)remote;
  (void)remote_port;

  // CONNECT - consume it whole, contents don't matter here.
  uint32_t remaining = 0;
  read_fixed_header(conn, &remaining);
  char discard[256];
  test_assert(remaining < sizeof(discard));
  read_exact(conn, discard, remaining);

  // CONNACK: accepted, no session present.
  write_exact(conn, (const uint8_t[]){0x20, 0x02, 0x00, 0x00}, 4);

  // PUBLISH (QoS 1): variable header is Topic Name (2-byte length +
  // bytes) then a 2-byte Packet Identifier - no need to look at whatever
  // payload follows.
  uint8_t publish_type = read_fixed_header(conn, &remaining);
  test_assert((publish_type & 0xF0) == 0x30);
  char publish_buf[256];
  test_assert(remaining <= sizeof(publish_buf));
  read_exact(conn, publish_buf, remaining);
  size_t topic_len =
      ((size_t)(uint8_t)publish_buf[0] << 8) | (uint8_t)publish_buf[1];
  uint16_t packet_id =
      (uint16_t)(((uint8_t)publish_buf[2 + topic_len] << 8) |
                (uint8_t)publish_buf[2 + topic_len + 1]);

  // The whole point of this test: reply only *after* the client's own
  // timeout_ms has already elapsed and it's given up locally.
  sys_sleep_ms(NET_022_LATE_PUBACK_DELAY_MS);

  uint8_t puback[4] = {0x40, 0x02, (uint8_t)(packet_id >> 8),
                       (uint8_t)(packet_id & 0xFF)};
  write_exact(conn, puback, sizeof(puback));

  sys_atomic_set(&g_broker_done, 1);
  // Deliberately doesn't close conn - the test's own net_listener_deinit()
  // cleans it up. Closing here would itself produce a real (and correct)
  // net_mqtt_event_disconnected once the client notices via
  // sys_iostream_eof() - exactly what net_021 tests - which would
  // confuse this test's own "the late PUBACK alone didn't disconnect
  // anything" assertion below.
}

test_main_sys(0) {
  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  net_listener_t *listener = net_listener_init(net_proto_tcp, &loopback,
                                               NET_022_PORT, on_accept, NULL);
  if (listener == NULL) {
    sys_printf("[net_022] no real net backend on this platform\n");
    return;
  }

  net_mqtt_t *mqtt = net_mqtt_init(&loopback, NET_022_PORT,
                                   NET_022_MQTT_TIMEOUT_MS, NULL);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);
  test_assert(net_mqtt_connect(mqtt));

  uint32_t id = net_mqtt_publish(mqtt, "picofuse/test/net_022", NULL, 0,
                                 net_mqtt_qos_1, false);
  test_assert(id != 0);

  // The local timeout should fire well before the fake broker's
  // deliberately-late PUBACK does.
  uint64_t start = sys_timestamp_ms();
  while (g_error_events == 0 && sys_timestamp_ms() - start < NET_022_WAIT_MS) {
    net_poll();
    sys_sleep_ms(NET_022_POLL_MS);
  }
  test_assert(g_error_events == 1);
  test_assert(g_last_error_message_id == id);
  test_assert(g_sent_events == 0);
  test_assert(g_disconnected_events == 0); // A timeout alone doesn't tear
                                           // down the connection - see
                                           // _net_mqtt_poll_publish_check_timeout()'s
                                           // own doc.

  // Now the late PUBACK arrives. Before the fix, reading it tripped a
  // sys_assert() and killed the process - if we're still executing at
  // all, that alone is already proof of the fix; also confirm it
  // produced no spurious second event and didn't tear the connection
  // down either.
  start = sys_timestamp_ms();
  while (sys_atomic_get(&g_broker_done) == 0 &&
        sys_timestamp_ms() - start < NET_022_WAIT_MS) {
    sys_sleep_ms(NET_022_POLL_MS);
  }
  test_assert(sys_atomic_get(&g_broker_done) != 0);

  start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < 500) {
    net_poll();
    sys_sleep_ms(NET_022_POLL_MS);
  }
  test_assert(g_sent_events == 0); // The late PUBACK is discarded, not
                                   // retroactively completed.
  test_assert(g_error_events == 1); // No second error event either.
  test_assert(g_disconnected_events == 0); // Connection survives.

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  net_listener_deinit(listener);
  sys_printf("[net_022] late PUBACK after timeout handled without crashing\n");
}

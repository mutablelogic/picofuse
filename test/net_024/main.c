#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Keepalive: net_poll() automatically sends a PINGREQ once
// net_mqtt_config_t::keepalive_s has elapsed with no other traffic, and
// reading the PINGRESP back doesn't abort the connection (a real crash
// this project's own review caught - PINGRESP was previously an
// unhandled, "unexpected packet type" case that tore the connection
// down on arrival, which would have made keepalive actively
// counterproductive once implemented). Uses a tiny keepalive_s (public,
// runtime-configurable - see its own doc) against a small local fake
// broker (loopback listener) so this stays fast and deterministic rather
// than needing a real ~60-second wait - see net_022's own comment on
// this pattern. Skips gracefully wherever there's no real backend yet -
// see net_002's own comment.

#define NET_024_PORT 17824
#define NET_024_KEEPALIVE_S 1
#define NET_024_MQTT_TIMEOUT_MS 1000
#define NET_024_WAIT_MS (5 * 1000)
#define NET_024_POLL_MS 20

static int g_error_events = 0;
static int g_disconnected_events = 0;
static sys_atomic_t g_broker_saw_pingreq;

static void on_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                     void *userdata) {
  (void)mqtt;
  (void)userdata;
  if (event->type == net_mqtt_event_error) {
    g_error_events++;
  } else if (event->type == net_mqtt_event_disconnected) {
    g_disconnected_events++;
  }
}

static uint8_t read_fixed_header(sys_iostream_t *conn, uint32_t *out_remaining) {
  uint8_t type_byte = 0;
  while (sys_iostream_read(conn, (char *)&type_byte, 1) == 0) {
    sys_sleep_ms(NET_024_POLL_MS);
  }
  uint32_t value = 0, multiplier = 1;
  for (;;) {
    uint8_t b = 0;
    while (sys_iostream_read(conn, (char *)&b, 1) == 0) {
      sys_sleep_ms(NET_024_POLL_MS);
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
      sys_sleep_ms(NET_024_POLL_MS);
    }
  }
}

static void write_exact(sys_iostream_t *conn, const uint8_t *buf, size_t n) {
  size_t wrote = 0;
  while (wrote < n) {
    wrote += sys_iostream_write(conn, (const char *)buf + wrote, n - wrote);
  }
}

// Plays just enough of a broker to get net_mqtt_connect() to succeed,
// then waits for (and answers) exactly one PINGREQ.
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

  // PINGREQ: fixed header only, Remaining Length always 0.
  uint8_t pingreq_type = read_fixed_header(conn, &remaining);
  test_assert(pingreq_type == 0xC0);
  test_assert(remaining == 0);
  sys_atomic_set(&g_broker_saw_pingreq, 1);

  write_exact(conn, (const uint8_t[]){0xD0, 0x00}, 2); // PINGRESP

  // Deliberately doesn't close conn - the test's own net_listener_deinit()
  // cleans it up, same reasoning as net_022's own comment.
}

test_main_sys(0) {
  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  net_listener_t *listener = net_listener_init(net_proto_tcp, &loopback,
                                               NET_024_PORT, on_accept, NULL);
  if (listener == NULL) {
    sys_printf("[net_024] no real net backend on this platform\n");
    return;
  }

  net_mqtt_config_t config = {.keepalive_s = NET_024_KEEPALIVE_S};
  net_mqtt_t *mqtt = net_mqtt_init(&loopback, NET_024_PORT,
                                   NET_024_MQTT_TIMEOUT_MS, &config);
  test_assert(mqtt != NULL);
  net_mqtt_set_callback(mqtt, on_event, NULL);
  test_assert(net_mqtt_connect(mqtt));

  // The automatic PINGREQ is due NET_024_KEEPALIVE_S after connecting -
  // poll until the fake broker confirms it actually arrived (and replied
  // to it).
  uint64_t start = sys_timestamp_ms();
  while (sys_atomic_get(&g_broker_saw_pingreq) == 0 &&
        sys_timestamp_ms() - start < NET_024_WAIT_MS) {
    net_poll();
    sys_sleep_ms(NET_024_POLL_MS);
  }
  test_assert(sys_atomic_get(&g_broker_saw_pingreq) != 0);

  // Give the client time to actually read the PINGRESP back and confirm
  // that doing so - the thing the review comment this test exists for
  // flagged as a real crash - doesn't abort the connection or report any
  // error.
  start = sys_timestamp_ms();
  while (sys_timestamp_ms() - start < 500) {
    net_poll();
    sys_sleep_ms(NET_024_POLL_MS);
  }
  test_assert(g_error_events == 0);
  test_assert(g_disconnected_events == 0);

  net_mqtt_disconnect(mqtt);
  net_mqtt_deinit(mqtt);
  net_listener_deinit(listener);
  sys_printf("[net_024] automatic PINGREQ/PINGRESP round trip OK\n");
}

#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_open()/net_listener_init(net_proto_udp, ...) round trip over
// loopback: a real received datagram, the single-shot stream contract
// (one read, one reply write, then closed - see net/net.h's own doc), and
// a second, independent datagram after the first stream is gone. Skips
// gracefully wherever there's no real backend yet - see net_002's own
// comment.

#define NET_003_PORT 17802
#define NET_003_WAIT_MS (5 * 1000)
#define NET_003_POLL_MS 20

// See net_002/main.c's own comment on why this needs sys_atomic_t rather
// than a plain (volatile or not) int - g_dgrams++ below is a
// read-modify-write on top of that, so a plain int risks a genuine lost
// update, not just a formal visibility issue.
static sys_atomic_t g_dgrams;

static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
                      const net_addr_t *remote, uint16_t remote_port,
                      void *userdata) {
  (void)listener;
  (void)userdata;
  (void)remote_port; // only referenced via sys_debugf(), a no-op in release
  char addrbuf[64];
  net_addr_to_string(remote, addrbuf, sizeof(addrbuf));
  sys_debugf("net_003", "datagram from %s:%u", addrbuf, (unsigned)remote_port);

  char buf[16] = {0};
  size_t n = sys_iostream_read(conn, buf, sizeof(buf) - 1);
  test_assert(n == 4);
  test_assert(memcmp(buf, "ping", 4) == 0);

  // A second read on the same (single-shot) stream must return nothing.
  test_assert(sys_iostream_read(conn, buf, sizeof(buf)) == 0);

  test_assert(sys_iostream_write(conn, "pong", 4) == 4);
  sys_iostream_close(conn);
  sys_atomic_inc(&g_dgrams);
}

static bool send_and_wait_reply(net_addr_t *loopback, int expect_count) {
  sys_iostream_t *client = net_open(net_proto_udp, loopback, NET_003_PORT, 0);
  if (client == NULL) {
    return false;
  }
  test_assert(sys_iostream_write(client, "ping", 4) == 4);

  char buf[16] = {0};
  size_t got = 0;
  uint64_t start = sys_timestamp_ms();
  while (got < 4 && sys_timestamp_ms() - start < NET_003_WAIT_MS) {
    got += sys_iostream_read(client, buf + got, sizeof(buf) - got);
    if (got < 4) {
      sys_sleep_ms(NET_003_POLL_MS);
    }
  }
  test_assert(got == 4);
  test_assert(memcmp(buf, "pong", 4) == 0);
  sys_iostream_close(client);

  start = sys_timestamp_ms();
  while ((int)sys_atomic_get(&g_dgrams) < expect_count &&
        sys_timestamp_ms() - start < NET_003_WAIT_MS) {
    sys_sleep_ms(NET_003_POLL_MS);
  }
  test_assert((int)sys_atomic_get(&g_dgrams) == expect_count);
  return true;
}

test_main_sys(0) {
  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  net_listener_t *listener = net_listener_init(net_proto_udp, &loopback,
                                               NET_003_PORT, on_accept, NULL);
  if (listener == NULL) {
    sys_printf("[net_003] no real net backend on this platform\n");
    return;
  }

  test_assert(send_and_wait_reply(&loopback, 1));
  // Independent datagram, exercising a second, freshly-allocated
  // single-shot stream from the same listener.
  test_assert(send_and_wait_reply(&loopback, 2));

  net_listener_deinit(listener);
}

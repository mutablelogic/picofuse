#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_open()/net_listener_init(net_proto_tcp, ...) round trip over
// loopback: a real accepted connection, a real bidirectional exchange, and
// stream teardown. Skips gracefully wherever there's no real backend yet
// (Pico's net/pico/socket.c is a stub - see its own @todo), the same way
// hw_016 skips wherever there's no real Wi-Fi backend.

#define NET_002_PORT 17801
#define NET_002_WAIT_MS (5 * 1000)
#define NET_002_POLL_MS 20

// net/posix/listener.c runs on_accept() on a real background thread (see
// its own _net_listener_tcp_thread()) - a plain bool, volatile or not,
// gives no cross-thread visibility/atomicity guarantee in C, so this
// needs the same sys_atomic_t signaling idiom the rest of the codebase
// already uses for exactly this (see e.g. test/sys_022/main.c's
// _holder_ready).
static sys_atomic_t g_accepted;

static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
                      const net_addr_t *remote, uint16_t remote_port,
                      void *userdata) {
  (void)listener;
  (void)userdata;
  (void)remote_port; // only referenced via sys_debugf(), a no-op in release
  char addrbuf[64];
  net_addr_to_string(remote, addrbuf, sizeof(addrbuf));
  sys_debugf("net_002", "accepted from %s:%u", addrbuf, (unsigned)remote_port);

  // A connection being accepted doesn't guarantee the client's first
  // write has arrived yet - retry rather than assuming one read() call
  // gets everything, same as the main test body's own client-side reads.
  char buf[16] = {0};
  size_t n = 0;
  uint64_t start = sys_timestamp_ms();
  while (n < 4 && sys_timestamp_ms() - start < NET_002_WAIT_MS) {
    n += sys_iostream_read(conn, buf + n, sizeof(buf) - n);
    if (n < 4) {
      sys_sleep_ms(NET_002_POLL_MS);
    }
  }
  test_assert(n == 4);
  test_assert(memcmp(buf, "ping", 4) == 0);

  test_assert(sys_iostream_write(conn, "pong", 4) == 4);
  sys_iostream_close(conn);
  sys_atomic_set(&g_accepted, 1);
}

test_main_sys(0) {
  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  net_listener_t *listener = net_listener_init(net_proto_tcp, &loopback,
                                               NET_002_PORT, on_accept, NULL);
  if (listener == NULL) {
    sys_printf("[net_002] no real net backend on this platform\n");
    return;
  }

  // NULL-safety, now that we know listeners work here at all.
  test_assert(net_open(net_proto_tcp, NULL, NET_002_PORT, 0) == NULL);

  sys_iostream_t *client = NULL;
  uint64_t start = sys_timestamp_ms();
  while (client == NULL &&
        sys_timestamp_ms() - start < NET_002_WAIT_MS) {
    client = net_open(net_proto_tcp, &loopback, NET_002_PORT, 0);
    if (client == NULL) {
      sys_sleep_ms(NET_002_POLL_MS);
    }
  }
  test_assert(client != NULL);

  test_assert(sys_iostream_write(client, "ping", 4) == 4);

  char buf[16] = {0};
  size_t got = 0;
  start = sys_timestamp_ms();
  while (got < 4 && sys_timestamp_ms() - start < NET_002_WAIT_MS) {
    got += sys_iostream_read(client, buf + got, sizeof(buf) - got);
    if (got < 4) {
      sys_sleep_ms(NET_002_POLL_MS);
    }
  }
  test_assert(got == 4);
  test_assert(memcmp(buf, "pong", 4) == 0);
  sys_iostream_close(client);

  start = sys_timestamp_ms();
  while (sys_atomic_get(&g_accepted) == 0 &&
        sys_timestamp_ms() - start < NET_002_WAIT_MS) {
    sys_sleep_ms(NET_002_POLL_MS);
  }
  test_assert(sys_atomic_get(&g_accepted) != 0);

  net_listener_deinit(listener);
  net_listener_deinit(NULL); // must not crash
}

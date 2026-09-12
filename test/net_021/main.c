#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// sys_iostream_eof() - detects a peer-closed TCP connection, distinct
// from "nothing to read yet" (see sys_iostream_eof()'s own doc on why
// sys_iostream_peek()/sys_iostream_read() alone can't tell those apart).
// Runs over a local loopback listener/connection so it works the same on
// every backend (POSIX here, and pico once built for one - see net_008's
// own comment on this pattern). Skips gracefully wherever there's no
// real backend yet - see net_002's own comment.

#define NET_021_PORT 17821
#define NET_021_WAIT_MS (5 * 1000)
#define NET_021_POLL_MS 20

static sys_atomic_t g_accepted;

static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
                      const net_addr_t *remote, uint16_t remote_port,
                      void *userdata) {
  (void)listener;
  (void)userdata;
  (void)remote;
  (void)remote_port;
  // Closes immediately, without ever writing anything - simulates a peer
  // that's done with the connection while it's otherwise idle.
  sys_iostream_close(conn);
  sys_atomic_set(&g_accepted, 1);
}

test_main_sys(0) {
  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  net_listener_t *listener = net_listener_init(net_proto_tcp, &loopback,
                                               NET_021_PORT, on_accept, NULL);
  if (listener == NULL) {
    sys_printf("[net_021] no real net backend on this platform\n");
    return;
  }

  sys_iostream_t *client = NULL;
  uint64_t start = sys_timestamp_ms();
  while (client == NULL && sys_timestamp_ms() - start < NET_021_WAIT_MS) {
    client = net_open(net_proto_tcp, &loopback, NET_021_PORT, 0);
    if (client == NULL) {
      sys_sleep_ms(NET_021_POLL_MS);
    }
  }
  test_assert(client != NULL);

  // Not eof yet, right after connecting.
  test_assert(!sys_iostream_eof(client));

  start = sys_timestamp_ms();
  while (sys_atomic_get(&g_accepted) == 0 &&
        sys_timestamp_ms() - start < NET_021_WAIT_MS) {
    sys_sleep_ms(NET_021_POLL_MS);
  }
  test_assert(sys_atomic_get(&g_accepted) != 0);

  // The server closed its end without ever writing anything -
  // sys_iostream_peek()/sys_iostream_read() alone would just look like
  // "nothing available yet" forever; sys_iostream_eof() is what actually
  // distinguishes this from a merely-idle, still-open connection.
  bool eof = false;
  start = sys_timestamp_ms();
  while (!eof && sys_timestamp_ms() - start < NET_021_WAIT_MS) {
    eof = sys_iostream_eof(client);
    if (!eof) {
      sys_sleep_ms(NET_021_POLL_MS);
    }
  }
  test_assert(eof);

  sys_iostream_close(client);
  net_listener_deinit(listener);
}

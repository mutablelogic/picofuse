#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// TCP backpressure: a payload bigger than the POSIX backend's own
// internal RX ring buffer (net/posix/socket.c's NET_CONN_BUFFER_SIZE,
// 512 bytes by default - not exposed publicly, so this uses a payload
// comfortably larger than any reasonable choice of it) must still arrive
// byte-for-byte, even when the reader doesn't drain it until well after
// the writer has sent everything. Before the fix, the background RX
// thread's recv() call could pull more bytes off the socket than the
// ring buffer had room for, silently dropping whatever didn't fit -
// this reproduces that exact scenario. Skips gracefully wherever there's
// no real backend yet - see net_002's own comment.

#define NET_008_PORT 17803
#define NET_008_PAYLOAD_SIZE 8192
#define NET_008_WAIT_MS (5 * 1000)
#define NET_008_POLL_MS 20
#define NET_008_READER_DELAY_MS 500

static uint8_t g_sent[NET_008_PAYLOAD_SIZE];
static uint8_t g_received[NET_008_PAYLOAD_SIZE];
static sys_atomic_t g_done;

static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
                      const net_addr_t *remote, uint16_t remote_port,
                      void *userdata) {
  (void)listener;
  (void)userdata;
  (void)remote;
  (void)remote_port;

  // Don't drain anything for a while - the writer sends the whole
  // payload well within this window, so the background RX thread has to
  // cope with far more arriving than NET_CONN_BUFFER_SIZE can hold
  // before anyone reads it.
  sys_sleep_ms(NET_008_READER_DELAY_MS);

  size_t got = 0;
  uint64_t start = sys_timestamp_ms();
  while (got < NET_008_PAYLOAD_SIZE &&
        sys_timestamp_ms() - start < NET_008_WAIT_MS) {
    got += sys_iostream_read(conn, (char *)g_received + got,
                             NET_008_PAYLOAD_SIZE - got);
    if (got < NET_008_PAYLOAD_SIZE) {
      sys_sleep_ms(NET_008_POLL_MS);
    }
  }
  test_assert(got == NET_008_PAYLOAD_SIZE);
  test_assert(memcmp(g_received, g_sent, NET_008_PAYLOAD_SIZE) == 0);

  sys_iostream_close(conn);
  sys_atomic_set(&g_done, 1);
}

test_main_sys(0) {
  for (size_t i = 0; i < NET_008_PAYLOAD_SIZE; i++) {
    g_sent[i] = (uint8_t)(i & 0xFF);
  }

  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  net_listener_t *listener = net_listener_init(net_proto_tcp, &loopback,
                                               NET_008_PORT, on_accept, NULL);
  if (listener == NULL) {
    sys_printf("[net_008] no real net backend on this platform\n");
    return;
  }

  sys_iostream_t *client = NULL;
  uint64_t start = sys_timestamp_ms();
  while (client == NULL &&
        sys_timestamp_ms() - start < NET_008_WAIT_MS) {
    client = net_open(net_proto_tcp, &loopback, NET_008_PORT);
    if (client == NULL) {
      sys_sleep_ms(NET_008_POLL_MS);
    }
  }
  test_assert(client != NULL);

  size_t sent = 0;
  while (sent < NET_008_PAYLOAD_SIZE) {
    size_t wrote =
        sys_iostream_write(client, (char *)g_sent + sent, NET_008_PAYLOAD_SIZE - sent);
    test_assert(wrote > 0);
    sent += wrote;
  }
  sys_iostream_close(client);

  start = sys_timestamp_ms();
  while (sys_atomic_get(&g_done) == 0 &&
        sys_timestamp_ms() - start < NET_008_WAIT_MS) {
    sys_sleep_ms(NET_008_POLL_MS);
  }
  test_assert(sys_atomic_get(&g_done) != 0);

  net_listener_deinit(listener);
}

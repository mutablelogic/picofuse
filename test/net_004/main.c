#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Failure/edge cases: NULL-safety, NET_LISTENER_CAPACITY exhaustion, and
// rejecting a second listener bound to an already-in-use address/port.
// Skips gracefully wherever there's no real backend yet - see net_002's
// own comment.

#define NET_004_BASE_PORT 17810

static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
                      const net_addr_t *remote, uint16_t remote_port,
                      void *userdata) {
  (void)listener;
  (void)remote;
  (void)remote_port;
  (void)userdata;
  sys_iostream_close(conn); // never expected to actually fire in this test
}

test_main_sys(0) {
  net_addr_t loopback = net_addr_v4(127, 0, 0, 1);

  // NULL-safety.
  test_assert(net_open(net_proto_tcp, NULL, NET_004_BASE_PORT) == NULL);
  test_assert(net_listener_init(net_proto_tcp, NULL, NET_004_BASE_PORT,
                                on_accept, NULL) == NULL);
  test_assert(net_listener_init(net_proto_tcp, &loopback, NET_004_BASE_PORT,
                                NULL, NULL) == NULL);
  net_listener_deinit(NULL); // must not crash

  // Probe for a real backend before relying on capacity/port-reuse
  // behavior below, which needs one.
  net_listener_t *probe = net_listener_init(
      net_proto_tcp, &loopback, NET_004_BASE_PORT, on_accept, NULL);
  if (probe == NULL) {
    sys_printf("[net_004] no real net backend on this platform\n");
    return;
  }

  // A second listener on the exact same protocol/address/port must fail.
  test_assert(net_listener_init(net_proto_tcp, &loopback, NET_004_BASE_PORT,
                                on_accept, NULL) == NULL);
  net_listener_deinit(probe);

  // NET_LISTENER_CAPACITY listeners, each on its own port, must all
  // succeed; one more beyond that must fail.
  net_listener_t *pool[NET_LISTENER_CAPACITY];
  for (int i = 0; i < NET_LISTENER_CAPACITY; i++) {
    pool[i] = net_listener_init(net_proto_tcp, &loopback,
                                (uint16_t)(NET_004_BASE_PORT + 1 + i),
                                on_accept, NULL);
    test_assert(pool[i] != NULL);
  }
  test_assert(net_listener_init(net_proto_tcp, &loopback,
                                (uint16_t)(NET_004_BASE_PORT + 1 +
                                          NET_LISTENER_CAPACITY),
                                on_accept, NULL) == NULL);
  for (int i = 0; i < NET_LISTENER_CAPACITY; i++) {
    net_listener_deinit(pool[i]);
  }

  // A pool slot freed by net_listener_deinit() must be reusable.
  net_listener_t *reused = net_listener_init(
      net_proto_tcp, &loopback, NET_004_BASE_PORT, on_accept, NULL);
  test_assert(reused != NULL);
  net_listener_deinit(reused);

  // Connecting to a port nothing is listening on must fail (fast, over
  // loopback) rather than hang or silently "succeed".
  test_assert(net_open(net_proto_tcp, &loopback,
                       (uint16_t)(NET_004_BASE_PORT + 999)) == NULL);
}

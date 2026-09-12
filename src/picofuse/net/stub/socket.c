#include <picofuse/net.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no networking backend on this platform/configuration. */
sys_iostream_t *net_open(net_proto_t proto, const net_addr_t *addr,
                         uint16_t port, uint32_t timeout_ms) {
  (void)proto;
  (void)addr;
  (void)port;
  (void)timeout_ms;
  sys_debugf("net", "net_open: not implemented on this target");
  return NULL;
}

/** Stub implementation: no networking backend on this platform/configuration. */
net_listener_t *net_listener_init(net_proto_t proto, const net_addr_t *addr,
                                  uint16_t port,
                                  net_accept_callback_t callback,
                                  void *userdata) {
  (void)proto;
  (void)addr;
  (void)port;
  (void)callback;
  (void)userdata;
  sys_debugf("net", "net_listener_init: not implemented on this target");
  return NULL;
}

/** Stub implementation: net_listener_init() never returns a non-NULL
 * handle on this target, so this is only ever called with NULL - a
 * no-op. */
void net_listener_deinit(net_listener_t *listener) { (void)listener; }

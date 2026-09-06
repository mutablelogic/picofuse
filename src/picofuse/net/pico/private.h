#pragma once
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include "lwip/ip_addr.h"
#include "lwip/tcp.h"

///////////////////////////////////////////////////////////////////////////////
// SHARED INTERNAL HELPERS
//
// socket.c/listener.c are part of the same backend and share these across
// translation units via this header, the same way the POSIX backend's own
// net/posix/posix.h does.
//
// Threading model: unlike the POSIX backend (a real OS thread per
// connection), this backend has none - lwIP is built here with NO_SYS=1
// and linked via pico_cyw43_arch_lwip_poll (see the top-level
// CMakeLists.txt), so every raw-API call (tcp_*/udp_*/pbuf_*) and every
// sys_iostream_t op on a stream this backend hands out must happen on
// whatever single thread/core calls hw_poll() - that's the only thing
// that ever pumps cyw43_arch_poll(), which is what actually drives a
// tcp_recv()/tcp_err()/tcp_accept()/udp_recv() callback. Calling any
// net_*() function, or touching a stream it returned, from a different
// core than the one driving hw_poll() is the caller's problem, same
// constraint hw_wifi_t's own doc already documents for the same reason -
// see include/picofuse/hw/wifi.h.

// Defined in socket.c. v4-only - returns false for net_addr_family_v6 (no
// IPv6 on Pico; this project's lwipopts.h only enables LWIP_IPV4 - see
// hw_wifi_get_address()'s own doc for the same constraint).
bool _net_addr_to_ipaddr(const net_addr_t *addr, ip_addr_t *out);

// Defined in socket.c. Reverse of _net_addr_to_ipaddr() - always
// produces a net_addr_family_v4 address (the only kind this backend ever
// deals in).
void _net_ipaddr_to_addr(const ip_addr_t *ip, net_addr_t *addr);

// Defined in socket.c. Wraps an already-accepted TCP pcb (from a
// listener's tcp_accept() callback) as a sys_iostream_t, using the same
// connected-stream machinery net_open() itself uses for its own TCP
// connections. Takes ownership of pcb - aborts it on any failure path.
// Returns NULL if the connected-stream pool (NET_CONN_CAPACITY) is full
// or the sys_iostream_t pool itself is exhausted.
sys_iostream_t *_net_conn_wrap_tcp(struct tcp_pcb *pcb);

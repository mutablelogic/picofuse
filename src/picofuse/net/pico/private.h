#pragma once
#include "lwip/ip_addr.h"
#include "lwip/tcp.h"
#include <picofuse/net.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// SHARED INTERNAL HELPERS

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

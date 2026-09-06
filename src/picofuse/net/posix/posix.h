#pragma once
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <sys/socket.h>

///////////////////////////////////////////////////////////////////////////////
// SHARED INTERNAL HELPERS
//
// socket.c/listener.c are part of the same backend and share these across
// translation units via this header, the same way hid/private.h shares
// _hid_singleton() etc. across hid/*.c.

// Defined in socket.c. Fills *out/*out_len from addr/port, or returns
// false if addr->family isn't recognized.
bool _net_addr_to_sockaddr(const net_addr_t *addr, uint16_t port,
                           struct sockaddr_storage *out, socklen_t *out_len);

// Defined in socket.c. Reverse of _net_addr_to_sockaddr().
void _net_sockaddr_to_addr(const struct sockaddr_storage *sa,
                           net_addr_t *addr, uint16_t *port);

// Defined in socket.c. Wraps an already-connected (net_open()) or
// already-accepted (a TCP listener) fd as a sys_iostream_t, spinning up
// its background RX thread. Takes ownership of fd - closes it on any
// failure path.
sys_iostream_t *_net_wrap_connected_fd(int fd);

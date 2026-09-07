/**
 * @file net.h
 * @brief Network interfaces and applications
 * @defgroup Network Network
 * @ingroup Picofuse
 *
 * Data streams over TCP or UDP, to IPv4 and/or IPv6 peers - net_open()
 * connects out to a remote host, net_listener_init() accepts incoming
 * connections or datagrams. Every socket, in either direction, is a
 * plain sys_iostream_t (@ref System): reading, writing, closing, and
 * readiness notification all go through the same generic
 * sys_iostream_read()/_write()/_close()/_set_callback() API every other
 * stream in picofuse uses, not socket-specific calls.
 *
 * Connecting out to a remote host:
 *
 * @code
 * net_addr_t addr = net_addr_v4(93, 184, 215, 14); // example.com
 * sys_iostream_t *conn = net_open(net_proto_tcp, &addr, 80);
 * if (conn != NULL) {
 *   const char *req = "GET / HTTP/1.0\r\n\r\n";
 *   sys_iostream_write(conn, req, strlen(req));
 *   char buf[512];
 *   size_t n = sys_iostream_read(conn, buf, sizeof(buf));
 *   // ... use buf/n ...
 *   sys_iostream_close(conn);
 * }
 * @endcode
 *
 * Accepting incoming connections - net_listener_init() returns
 * immediately, and @p callback fires from then on for as long as the
 * listener stays open (once per accepted connection for
 * @ref net_proto_tcp, once per received datagram for
 * @ref net_proto_udp - see net_listener_init()'s own doc):
 *
 * @code
 * static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
 *                       const net_addr_t *remote, uint16_t remote_port,
 *                       void *userdata) {
 *   char buf[512];
 *   size_t n = sys_iostream_read(conn, buf, sizeof(buf));
 *   sys_iostream_write(conn, buf, n); // echo back
 *   sys_iostream_close(conn);
 * }
 *
 * net_addr_t any = net_addr_v4_any();
 * net_listener_t *echo =
 *     net_listener_init(net_proto_tcp, &any, 7, on_accept, NULL);
 * @endcode
 */
#pragma once
#include "net/net.h"
#include "net/ntp.h"
#include "net/types.h"

/**
 * @file net.h
 * @brief TCP/UDP network sockets.
 * @ingroup Network
 *
 * Every socket - an outgoing connection from net_open(), or an incoming
 * one accepted by net_listener_init() - is a plain sys_iostream_t
 * (picofuse/sys/io.h): reading, writing, closing, and readiness
 * notification all go through the generic sys_iostream_read()/_write()/
 * _close()/_set_callback() rather than socket-specific equivalents.
 *
 * net_open() connects out to a remote host - TCP or UDP.
 *
 * net_listener_init() accepts incoming traffic - TCP or UDP - and is
 * callback-driven, like every other asynchronous source in picofuse
 * (hw_gpio_set_callback(), sys_iostream_set_callback(),
 * hw_wifi_set_callback(), ...): it returns immediately with a
 * net_listener_t, and @p callback fires from then on with a stream to
 * read/write, plus the remote address it's with:
 * - For @ref net_proto_tcp, once per accepted connection - @p conn is a
 *   genuine long-lived stream for that connection's whole lifetime.
 * - For @ref net_proto_udp, once per received datagram - @p conn is a
 *   short-lived stream good for exactly one sys_iostream_read() (that
 *   datagram's payload) and, if a reply is wanted, one
 *   sys_iostream_write() back to the same sender, then
 *   sys_iostream_close(). This is what a DNS or DHCP server (answering
 *   arbitrary, not-yet-known clients) is built on, since it needs
 *   per-datagram addressing rather than one fixed peer.
 *
 * @code
 * // TCP or UDP server - same shape either way
 * static void on_accept(net_listener_t *listener, sys_iostream_t *conn,
 *                       const net_addr_t *remote, uint16_t remote_port,
 *                       void *userdata) {
 *   char buf[512];
 *   size_t n = sys_iostream_read(conn, buf, sizeof(buf));
 *   size_t reply_len = my_handle_request(buf, n, ...);
 *   sys_iostream_write(conn, buf, reply_len);
 *   sys_iostream_close(conn);
 * }
 *
 * net_addr_t any = net_addr_v4_any();
 * net_listener_t *dns =
 *     net_listener_init(net_proto_udp, &any, 53, on_accept, NULL);
 * @endcode
 */
#pragma once
#include <picofuse/net/types.h>
#include <picofuse/sys/io.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// ADDRESSES

/** @name Addresses
 * @{ */

/**
 * @brief Build an IPv4 address from four octets.
 * @ingroup Network
 */
net_addr_t net_addr_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/**
 * @brief The IPv4 "any" address (0.0.0.0), for binding to all interfaces.
 * @ingroup Network
 */
net_addr_t net_addr_v4_any(void);

/**
 * @brief Build an IPv6 address from sixteen bytes.
 * @ingroup Network
 * @param bytes Address bytes, network byte order. Must point to at least
 * 16 bytes.
 */
net_addr_t net_addr_v6(const uint8_t bytes[16]);

/**
 * @brief The IPv6 "any" address (::), for binding to all interfaces.
 * @ingroup Network
 */
net_addr_t net_addr_v6_any(void);

/**
 * @brief Format an address as a human-readable string.
 * @ingroup Network
 * @param addr Address to format.
 * @param buf Destination buffer.
 * @param buf_size Size of @p buf in bytes.
 * @return Number of characters that would have been written to @p buf, not
 * counting the null terminator, same truncation semantics as
 * sys_sprintf().
 */
size_t net_addr_to_string(const net_addr_t *addr, char *buf, size_t buf_size);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Open a connection to a remote host.
 * @ingroup Network
 * @param proto Transport protocol.
 * @param addr Remote address to connect to.
 * @param port Remote port to connect to.
 * @return An open stream, or NULL on failure (connection refused, timed
 * out, or no route). Blocks until connected or the attempt fails.
 *
 * For @ref net_proto_udp this "connects" the socket to a single remote
 * address: every subsequent sys_iostream_write() sends to it, and
 * sys_iostream_read() only ever returns datagrams from it. Use
 * net_listener_init() instead for a UDP socket that receives from arbitrary,
 * not-yet-known peers.
 */
sys_iostream_t *net_open(net_proto_t proto, const net_addr_t *addr,
                         uint16_t port);

/**
 * @brief Start listening for incoming connections or datagrams.
 * @ingroup Network
 * @param proto Transport protocol.
 * @param addr Local address to bind to (see net_addr_v4_any()/
 * net_addr_v6_any() to bind to all interfaces).
 * @param port Local port to bind to.
 * @param callback Called for each accepted connection or received
 * datagram (see @ref net_proto_t), for as long as the listener stays open.
 * @param userdata Opaque pointer passed through to @p callback.
 * @return A listener handle, or NULL on failure (for example, the port is
 * already in use, or @ref NET_LISTENER_CAPACITY listeners are already
 * open).
 */
net_listener_t *net_listener_init(net_proto_t proto, const net_addr_t *addr,
                                  uint16_t port,
                                  net_accept_callback_t callback,
                                  void *userdata);

/**
 * @brief Stop listening and release a listener.
 * @ingroup Network
 * @param listener The listener to close, or NULL (a no-op). Streams
 * already handed to @p callback are unaffected - see its own doc.
 */
void net_listener_deinit(net_listener_t *listener);

/** @} */

#ifdef __cplusplus
}
#endif

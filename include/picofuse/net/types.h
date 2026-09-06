/**
 * @file types.h
 * @brief Network module types.
 * @ingroup Network
 */
#pragma once
#include <picofuse/sys/io.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def NET_LISTENER_CAPACITY
 * @ingroup Network
 * @brief Maximum number of listeners open at once (see net_listener_init()).
 */
#ifndef NET_LISTENER_CAPACITY
#define NET_LISTENER_CAPACITY 4
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Transport protocol for a socket.
 * @ingroup Network
 */
typedef enum {
  net_proto_tcp, ///< Connection-oriented, reliable, ordered byte stream.
  net_proto_udp, ///< Connectionless, unreliable datagrams.
} net_proto_t;

/**
 * @brief Address family for a net_addr_t.
 * @ingroup Network
 */
typedef enum {
  net_addr_family_v4, ///< IPv4 - net_addr_t.addr.v4 is valid.
  net_addr_family_v6, ///< IPv6 - net_addr_t.addr.v6 is valid.
} net_addr_family_t;

/**
 * @brief An IPv4 or IPv6 address.
 * @ingroup Network
 */
typedef struct {
  net_addr_family_t family; ///< Which member of addr is valid.
  union {
    uint8_t v4[4];  ///< IPv4 address, network byte order.
    uint8_t v6[16]; ///< IPv6 address, network byte order.
  } addr;           ///< Address bytes - see family.
} net_addr_t;

/**
 * @brief Opaque listening socket, from net_listener_init().
 * @ingroup Network
 */
typedef struct net_listener_t net_listener_t;

/**
 * @brief Called for each accepted TCP connection or received UDP datagram
 * on a listener.
 * @ingroup Network
 * @param listener The listener this arrived on.
 * @param conn Stream to read/write. Caller-owned - close with
 * sys_iostream_close() when done with it; net_listener_deinit() on
 * @p listener does not affect streams already handed out this way. For
 * @ref net_proto_udp this is good for exactly one read and, if replying,
 * one write - see net.h's own doc for why.
 * @param remote Address of the remote peer.
 * @param remote_port Port of the remote peer.
 * @param userdata Opaque pointer, as passed to net_listener_init().
 */
typedef void (*net_accept_callback_t)(net_listener_t *listener,
                                      sys_iostream_t *conn,
                                      const net_addr_t *remote,
                                      uint16_t remote_port, void *userdata);

#ifdef __cplusplus
}
#endif

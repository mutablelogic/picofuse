#pragma once
#include "lwip/ip_addr.h"
#include "lwip/tcp.h"
#include <picofuse/net.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// SHARED INTERNAL HELPERS

/** @brief Converts a net_addr_t to an lwIP ip_addr_t.
 *
 * @param addr The source network address.
 * @param out The destination IP address.
 * @return true if the conversion was successful, false otherwise.
 */
bool _net_addr_to_ipaddr(const net_addr_t *addr, ip_addr_t *out);

/** @brief Converts an lwIP ip_addr_t to a net_addr_t.
 *
 * @param ip The source IP address.
 * @param addr The destination network address.
 */
void _net_ipaddr_to_addr(const ip_addr_t *ip, net_addr_t *addr);

/** @brief Wraps an already-accepted TCP pcb as a sys_iostream_t.
 *
 * @param pcb The TCP protocol control block to wrap.
 * @return A pointer to the sys_iostream_t representing the connection, or NULL
 * on failure.
 */
sys_iostream_t *_net_conn_wrap_tcp(struct tcp_pcb *pcb);

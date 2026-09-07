/**
 * @file ntp.h
 * @brief Simple SNTP time client.
 * @defgroup NetworkNTP NTP
 * @ingroup Network
 *
 * NTP (Network Time Protocol) is how a device gets the current wall-clock
 * time from a server over the network, rather than relying on a
 * battery-backed RTC it may not have.
 *
 * A net_ntp_t identifies one NTP server; net_ntp_read() opens a fresh
 * connection to it, sends one request, returns the time it replies with,
 * and closes the connection again - no socket is held open between
 * calls, so net_ntp_read() can be called as rarely (or as often) as you
 * like without tying up a connection slot in the meantime. This is a
 * plain SNTP client, not full NTP - it takes the server's transmit
 * timestamp directly rather than running NTP's own clock-offset/
 * round-trip-delay algorithm, which is enough for keeping a device's
 * clock roughly in sync but not for the sub-millisecond discipline full
 * NTP aims for.
 *
 * There's no built-in polling/retry interval here - call net_ntp_read()
 * as often as you want the time re-checked, or use net_ntp_register_hid()
 * for a periodic HID source built on top of this the same way
 * hid_register_temperature() is for ADC polling.
 *
 * @code
 * net_addr_t server = net_addr_v4(162, 159, 200, 1); // time.cloudflare.com
 * net_ntp_t *ntp = net_ntp_init(&server, NET_NTP_PORT, 2000);
 * if (ntp != NULL) {
 *   sys_date_t date;
 *   if (net_ntp_read(ntp, &date)) {
 *     sys_date_set_now(&date);
 *   }
 *   net_ntp_deinit(ntp);
 * }
 * @endcode
 */
#pragma once
#include <picofuse/hid/device.h>
#include <picofuse/net/types.h>
#include <picofuse/sys/date.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def NET_NTP_PORT
 * @ingroup NetworkNTP
 * @brief Standard NTP port.
 */
#define NET_NTP_PORT 123

/**
 * @def NET_NTP_DEFAULT_ADDR
 * @ingroup NetworkNTP
 * @brief Default NTP server address, as net_addr_v4() arguments - used by
 * net_ntp_init() when its own @p addr parameter is NULL.
 *
 * Defaults to time.cloudflare.com (162.159.200.1). Override at compile
 * time, for example: `-DNET_NTP_DEFAULT_ADDR="129,6,15,28"` (time.nist.gov).
 */
#ifndef NET_NTP_DEFAULT_ADDR
#define NET_NTP_DEFAULT_ADDR 162, 159, 200, 1
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque handle identifying an SNTP server to query.
 * @ingroup NetworkNTP
 */
typedef struct net_ntp_t net_ntp_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Identify an NTP server to query with net_ntp_read().
 * @ingroup NetworkNTP
 * @param addr Server address, or NULL to default to
 * time.cloudflare.com (162.159.200.1).
 * @param port Server port, or 0 to default to NET_NTP_PORT (123).
 * @param timeout_ms How long net_ntp_read() waits for a reply before
 * giving up, on every call made with the returned handle.
 * @return Handle for net_ntp_read()/net_ntp_deinit(), or NULL if another
 * handle is already active - see net_ntp_t's own doc on why there's no
 * pool.
 */
net_ntp_t *net_ntp_init(const net_addr_t *addr, uint16_t port,
                        uint32_t timeout_ms);

/**
 * @brief Release a handle from net_ntp_init().
 * @ingroup NetworkNTP
 * @param ntp Handle to release, or NULL (a no-op).
 */
void net_ntp_deinit(net_ntp_t *ntp);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Query the server for the current time.
 * @ingroup NetworkNTP
 * @param ntp Handle from net_ntp_init().
 * @param date Set to the server's reported time (UTC - tzoffset is
 * always 0) on success. Left untouched on failure.
 * @retval true @p date was filled in.
 * @retval false @p ntp or @p date was NULL, a connection couldn't be
 * opened, the request couldn't be sent, or no reply arrived within the
 * timeout given to net_ntp_init().
 *
 * Opens a fresh connection, blocks for up to that timeout waiting for a
 * reply, then closes it again before returning - see net_ntp_t's own doc
 * on why nothing is held open between calls. Safe to call repeatedly on
 * the same handle.
 */
bool net_ntp_read(net_ntp_t *ntp, sys_date_t *date);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// HID INTEGRATION

/** @name HID Integration
 * @{ */

/**
 * @brief Register an NTP connection as a polling HID time source.
 * @ingroup NetworkNTP
 * @param instance HID instance that owns the registration.
 * @param ntp Handle from net_ntp_init(). Not owned by this registration -
 * net_ntp_deinit() is still the caller's own responsibility, the same
 * non-owning relationship hid_register_wifi() has with its own hw_wifi_t.
 * @param polling_interval_ms Polling interval in milliseconds. Passing 0
 * uses a default interval of one hour - frequent enough to notice clock
 * drift, infrequent enough not to hammer the server.
 * @param userdata Opaque user data retrievable via hid_device_userdata()
 * on the returned device.
 * @return Registered HID device descriptor, or NULL on failure (@p ntp
 * was NULL, or an NTP HID source is already registered - see below).
 *
 * Calls net_ntp_read() on every poll and publishes a hid_event_type_time
 * event whenever the reported time has changed since the last poll (to
 * whole-second resolution). A singleton, not a pool, matching net_ntp_t's
 * own reasoning - only one registration can be active at a time.
 */
hid_device_t *net_ntp_register_hid(hid_t *instance, net_ntp_t *ntp,
                                   uint32_t polling_interval_ms,
                                   void *userdata);

/** @} */

#ifdef __cplusplus
}
#endif

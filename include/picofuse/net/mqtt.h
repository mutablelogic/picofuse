/**
 * @file mqtt.h
 * @brief Simple MQTT client.
 * @defgroup NetworkMQTT MQTT
 * @ingroup Network
 */
#pragma once
#include <picofuse/net/types.h>
#include <picofuse/sys/io.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def NET_MQTT_PORT
 * @ingroup NetworkMQTT
 * @brief Standard MQTT port.
 */
#define NET_MQTT_PORT 1883

/**
 * @def NET_MQTT_TOPIC_CAPACITY
 * @ingroup NetworkMQTT
 * @brief Maximum number of simultaneously active net_mqtt_subscribe()
 * topic filters, per handle.
 */
#ifndef NET_MQTT_TOPIC_CAPACITY
#define NET_MQTT_TOPIC_CAPACITY 8
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque handle identifying an MQTT client instance.
 * @ingroup NetworkMQTT
 *
 * One handle at a time, like net_ntp_t - a static singleton rather than a
 * pool, since a device typically has exactly one MQTT connection to
 * exactly one broker.
 */
typedef struct net_mqtt_t net_mqtt_t;

/**
 * @brief Delivery guarantee for a published or subscribed message.
 * @ingroup NetworkMQTT
 *
 * A property of each message, not the connection - see net_mqtt_config_t's
 * own doc on why there's no connection-wide default.
 */
typedef enum {
  net_mqtt_qos_0, ///< At most once - fire and forget, no acknowledgment.
  net_mqtt_qos_1, ///< At least once - acknowledged, but may be delivered
                  ///< more than once.
  net_mqtt_qos_2, ///< Exactly once - slowest, a four-way handshake under
                  ///< the hood.
} net_mqtt_qos_t;

/**
 * @brief MQTT connection options.
 * @ingroup NetworkMQTT
 *
 * Optional settings beyond the address/port/timeout passed directly to
 * net_mqtt_init(). When `NULL` is passed there, implementation defaults
 * are used for every field here. QoS is deliberately not one of them -
 * it's a per-message property of the MQTT protocol itself (each PUBLISH/
 * SUBSCRIBE carries its own), not a connection-level setting, so it'll be
 * a parameter on those calls rather than living here. Session state
 * (clean session vs. resumed) and keep-alive timing aren't exposed either
 * - the implementation always connects with a clean session and its own
 * fixed keep-alive interval.
 */
typedef struct {
  const char *client_id; ///< MQTT client identifier sent in CONNECT. `NULL`
                        ///< or empty auto-generates one.
} net_mqtt_config_t;

/**
 * @brief Event payload tag for net_mqtt_event_t.
 * @ingroup NetworkMQTT
 */
typedef enum {
  net_mqtt_event_connected,    ///< net_mqtt_connect() succeeded - carries
                               ///< no payload.
  net_mqtt_event_disconnected, ///< The connection closed, whether from
                               ///< net_mqtt_disconnect() or an unexpected
                               ///< drop noticed during net_poll() -
                               ///< carries no payload.
  net_mqtt_event_sent,        ///< A net_mqtt_publish() call completed -
                               ///< see net_mqtt_sent_t.
  net_mqtt_event_received,    ///< A message arrived on a subscribed
                               ///< topic - see net_mqtt_received_t.
  net_mqtt_event_error,       ///< Something failed - see net_mqtt_error_t.
} net_mqtt_event_type_t;

/**
 * @brief Payload for a net_mqtt_event_sent event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  const char *topic; ///< Topic that was published to.
} net_mqtt_sent_t;

/**
 * @brief Payload for a net_mqtt_event_received event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  const char *topic;      ///< Topic the message was published to - may be
                          ///< more specific than the net_mqtt_subscribe()
                          ///< filter that matched it, if that filter used
                          ///< a wildcard.
  sys_iostream_t *payload; ///< Message payload, as a stream, not a
                          ///< buffer - MQTT payloads have no protocol
                          ///< size limit, so this is read incrementally
                          ///< rather than requiring the whole message to
                          ///< be buffered in memory first. Not
                          ///< caller-owned and only valid for the
                          ///< duration of this callback - don't
                          ///< sys_iostream_close() it. Good for reading
                          ///< up to payload_len bytes total; whatever's
                          ///< left unread when the callback returns is
                          ///< discarded automatically, so there's no need
                          ///< to drain it fully.
  size_t payload_len;     ///< Total payload length in bytes, known
                          ///< upfront from the MQTT packet header.
  bool retain;            ///< True if this is a retained message
                          ///< delivered because of the subscription
                          ///< itself rather than a live publish - see
                          ///< net_mqtt_publish()'s own doc on retained
                          ///< messages.
} net_mqtt_received_t;

/**
 * @brief Payload for a net_mqtt_event_error event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  const char *message; ///< Human-readable description of what failed.
} net_mqtt_error_t;

/**
 * @brief A single MQTT client event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  net_mqtt_event_type_t type; ///< Event payload tag.
  union {
    net_mqtt_sent_t sent;
    net_mqtt_received_t received;
    net_mqtt_error_t error;
  } data; ///< Payload selected by type - connected/disconnected carry none.
} net_mqtt_event_t;

/**
 * @brief Called for every event on an MQTT client instance.
 * @ingroup NetworkMQTT
 * @param mqtt The handle the event occurred on.
 * @param event The event - see net_mqtt_event_t.
 * @param userdata Opaque pointer, as passed to net_mqtt_set_callback().
 *
 * Called from net_poll() - see its own doc (net.h) on the context this
 * runs in and what that means for what's safe to do here. net_mqtt_connect()/
 * net_mqtt_disconnect() also deliver their own connected/disconnected
 * event synchronously, before returning, in addition to their own bool
 * result - a caller that only reacts to state changes through this
 * callback doesn't also need to inspect those return values.
 */
typedef void (*net_mqtt_event_callback_t)(net_mqtt_t *mqtt,
                                          const net_mqtt_event_t *event,
                                          void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Fill an MQTT config struct with safe defaults.
 * @ingroup NetworkMQTT
 * @param config Config structure to initialize.
 *
 * Sets client_id to a stable, auto-generated id derived from the
 * environment's own name and serial number - the same one net_mqtt_init()
 * falls back to when passed `NULL` directly, so calling this first isn't
 * required. Useful for a caller that wants the defaults as a starting
 * point to then override just one or two fields.
 */
void net_mqtt_default_config(net_mqtt_config_t *config);

/**
 * @brief Initialize an MQTT client instance.
 * @ingroup NetworkMQTT
 * @param addr Server address - required. Unlike net_ntp_init(), there's
 * no single "standard" broker to default to.
 * @param port Server port, or 0 to default to NET_MQTT_PORT (1883).
 * @param timeout_ms How long the client waits for a reply before giving up,
 * on every call made with the returned handle.
 * @param config Optional pointer to extended connection settings - see
 * net_mqtt_config_t. Pass `NULL` for an auto-generated client_id (same as
 * net_mqtt_default_config()'s own defaults).
 * @return Handle for net_mqtt operations, or NULL if @p addr was `NULL`,
 * or another handle is already active - see net_mqtt_t's own doc on why
 * there's no pool.
 */
net_mqtt_t *net_mqtt_init(const net_addr_t *addr, uint16_t port,
                          uint32_t timeout_ms,
                          const net_mqtt_config_t *config);

/**
 * @brief Register the callback for events on an MQTT client instance.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init().
 * @param callback Called for every net_mqtt_event_t on @p mqtt - see
 * net_mqtt_event_callback_t. Pass `NULL` to stop receiving them.
 * @param userdata Opaque pointer passed to callback.
 *
 * One callback for the whole handle, covering every event type -
 * connects, disconnects, publish completions, incoming subscribed
 * messages, and errors alike; a caller branches on net_mqtt_event_t::type
 * rather than registering one callback per kind of event. Same shape as
 * pix_display_set_callback().
 */
void net_mqtt_set_callback(net_mqtt_t *mqtt,
                           net_mqtt_event_callback_t callback,
                           void *userdata);

/**
 * @brief Release a handle from net_mqtt_init().
 * @ingroup NetworkMQTT
 * @param mqtt Handle to release, or NULL (a no-op).
 *
 * Disconnects first (see net_mqtt_disconnect()) if still connected - there
 * is no need to call that separately before this.
 */
void net_mqtt_deinit(net_mqtt_t *mqtt);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// CONNECTION

/** @name Connection
 * @{ */

/**
 * @brief Open the MQTT connection.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init().
 * @retval true Connected - the underlying socket opened and the broker
 * acknowledged an MQTT CONNECT - within the handle's timeout_ms.
 * @retval false @p mqtt was NULL, was already connected, or the connection
 * didn't succeed within the handle's timeout_ms.
 *
 * Safe to call again - to reconnect after net_mqtt_disconnect() or an
 * unexpected drop - reusing the same client_id and other net_mqtt_init()
 * settings.
 */
bool net_mqtt_connect(net_mqtt_t *mqtt);

/**
 * @brief Close the MQTT connection.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), or NULL (a no-op). A no-op if
 * not currently connected.
 *
 * Sends an MQTT DISCONNECT (best-effort - the socket is closed regardless
 * of whether it's actually sent or acknowledged) and closes the
 * underlying socket. @p mqtt itself remains valid - call
 * net_mqtt_connect() again to reconnect, or net_mqtt_deinit() to release
 * it entirely.
 */
void net_mqtt_disconnect(net_mqtt_t *mqtt);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PUBLISH

/** @name Publish
 * @{ */

/**
 * @brief Publish a message to a topic.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), must be connected - see
 * net_mqtt_connect().
 * @param topic Topic to publish to.
 * @param payload Message payload. May be NULL if payload_len is 0, for an
 * empty message.
 * @param payload_len Length of payload in bytes.
 * @param qos Delivery guarantee for this message - see net_mqtt_qos_t.
 * @param retain If true, the broker keeps this message as the topic's
 * last-known value, delivered immediately to any client that subscribes
 * to it afterward - until replaced by another retained publish, or
 * cleared with a retained empty message.
 * @retval true Sent (net_mqtt_qos_0) or acknowledged within the handle's
 * timeout_ms (net_mqtt_qos_1/net_mqtt_qos_2) - a net_mqtt_event_sent
 * event also fires at that same point, for a caller tracking completion
 * through net_mqtt_set_callback() instead of this return value.
 * @retval false @p mqtt was NULL or not connected, @p topic was NULL, or
 * the send/acknowledgment didn't complete within timeout_ms - a
 * net_mqtt_event_error event fires in that last case.
 */
bool net_mqtt_publish(net_mqtt_t *mqtt, const char *topic, const void *payload,
                      size_t payload_len, net_mqtt_qos_t qos, bool retain);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// SUBSCRIBE

/** @name Subscribe
 * @{ */

/**
 * @brief Subscribe to a topic filter.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), must be connected.
 * @param topic Topic filter - may include MQTT wildcards (`+` for a
 * single level, `#` as a trailing multi-level match).
 * @param qos Maximum delivery guarantee requested for this subscription -
 * see net_mqtt_qos_t. The broker may grant a lower QoS than requested;
 * whichever it grants is what's actually used for messages delivered
 * under it.
 * @retval true Subscribed - the broker acknowledged (SUBACK) within the
 * handle's timeout_ms.
 * @retval false @p mqtt was NULL or not connected, @p topic was NULL,
 * NET_MQTT_TOPIC_CAPACITY active filters are already in use, or the
 * broker didn't acknowledge within timeout_ms.
 *
 * Messages matching this filter arrive - via net_poll() - as
 * net_mqtt_event_received events on whichever callback is currently
 * registered via net_mqtt_set_callback() - register that first, since
 * nothing is queued for a callback that isn't set yet.
 */
bool net_mqtt_subscribe(net_mqtt_t *mqtt, const char *topic,
                        net_mqtt_qos_t qos);

/**
 * @brief Unsubscribe from a topic filter.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), must be connected.
 * @param topic Topic filter previously passed to net_mqtt_subscribe() -
 * must match exactly, not just overlap.
 * @retval true Unsubscribed - broker acknowledged (UNSUBACK) within
 * timeout_ms.
 * @retval false @p mqtt was NULL or not connected, @p topic was NULL or
 * not currently subscribed, or the broker didn't acknowledge within
 * timeout_ms.
 */
bool net_mqtt_unsubscribe(net_mqtt_t *mqtt, const char *topic);

/** @} */

#ifdef __cplusplus
}
#endif

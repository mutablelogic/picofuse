/**
 * @file mqtt.h
 * @brief Simple MQTT client.
 * @defgroup NetworkMQTT MQTT
 * @ingroup Network
 */
#pragma once
#include <picofuse/net/types.h>
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
  const char *username;  ///< Optional username sent in CONNECT. `NULL` or
                         ///< empty sends none.
  const char *password;  ///< Optional password sent in CONNECT. Only ever
                         ///< sent alongside a non-empty username.
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
  net_mqtt_event_sent,         ///< A net_mqtt_publish() call completed -
                               ///< see net_mqtt_sent_t.
  net_mqtt_event_subscribed,   ///< A net_mqtt_subscribe() call completed -
                               ///< see net_mqtt_subscribed_t.
  net_mqtt_event_unsubscribed, ///< A net_mqtt_unsubscribe() call
                               ///< completed - see
                               ///< net_mqtt_unsubscribed_t.
  net_mqtt_event_received,     ///< A message arrived on a subscribed
                               ///< topic - see net_mqtt_received_t.
  net_mqtt_event_error,        ///< Something failed - see net_mqtt_error_t.
} net_mqtt_event_type_t;

/**
 * @brief Payload for a net_mqtt_event_sent event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  const char *topic;    ///< Topic that was published to.
  uint32_t message_id;  ///< The id net_mqtt_publish() returned for the
                        ///< call this event completes - lets a caller
                        ///< with several publishes in flight match each
                        ///< one to its own completion.
} net_mqtt_sent_t;

/**
 * @brief Payload for a net_mqtt_event_subscribed event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  net_mqtt_qos_t granted_qos; ///< The QoS the broker actually granted for
                             ///< this subscription - may be lower than
                             ///< what was requested, never higher.
  uint32_t message_id;       ///< The id net_mqtt_subscribe() returned for
                             ///< the call this event completes.
} net_mqtt_subscribed_t;

/**
 * @brief Payload for a net_mqtt_event_unsubscribed event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  uint32_t message_id; ///< The id net_mqtt_unsubscribe() returned for
                       ///< the call this event completes.
} net_mqtt_unsubscribed_t;

/**
 * @brief Payload for a net_mqtt_event_received event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  const char *topic;   ///< Topic the message was published to - may be
                       ///< more specific than the net_mqtt_subscribe()
                       ///< filter that matched it, if that filter used a
                       ///< wildcard. Not caller-owned, valid only for the
                       ///< duration of this callback.
  const void *payload; ///< Message payload - NULL if payload_len is 0.
                       ///< Not caller-owned and only valid for the
                       ///< duration of this callback - copy anything
                       ///< that's still needed once it returns, same as
                       ///< net_mqtt_publish()'s own @p payload parameter.
                       ///< Read off the wire and buffered in full before
                       ///< this callback runs (a small internal buffer
                       ///< for short messages, a temporary allocation
                       ///< freed right after this callback returns for
                       ///< larger ones) - not streamed, so there's no
                       ///< need to read it "incrementally" to avoid
                       ///< holding the whole message in memory.
  size_t payload_len;  ///< Total payload length in bytes.
  bool retain;         ///< True if this is a retained message delivered
                       ///< because of the subscription itself rather
                       ///< than a live publish - see net_mqtt_publish()'s
                       ///< own doc on retained messages.
} net_mqtt_received_t;

/**
 * @brief Payload for a net_mqtt_event_error event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  const char *message; ///< Human-readable description of what failed.
  uint32_t message_id; ///< The id net_mqtt_publish() returned for the
                       ///< call this error belongs to, if any - `0` if
                       ///< this error isn't tied to a specific publish
                       ///< (`0` is never a real net_mqtt_publish() id -
                       ///< see its own doc).
} net_mqtt_error_t;

/**
 * @brief A single MQTT client event.
 * @ingroup NetworkMQTT
 */
typedef struct {
  net_mqtt_event_type_t type; ///< Event payload tag.
  union {
    net_mqtt_sent_t sent;
    net_mqtt_subscribed_t subscribed;
    net_mqtt_unsubscribed_t unsubscribed;
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
 * required. username/password default to `NULL` (no authentication).
 * Useful for a caller that wants the defaults as a starting
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
                          uint32_t timeout_ms, const net_mqtt_config_t *config);

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
void net_mqtt_set_callback(net_mqtt_t *mqtt, net_mqtt_event_callback_t callback,
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
 * underlying socket. Every net_mqtt_subscribe()'d topic filter is
 * forgotten locally, without sending any UNSUBSCRIBE - the connection
 * always uses a clean session (see net_mqtt_config_t's own doc), so the
 * broker discards them on its own the moment the session ends; nothing
 * more is needed for a client that then calls net_mqtt_connect() again,
 * whether on this handle or another, to start with a clean slate.
 * @p mqtt itself remains valid - call net_mqtt_connect() again to
 * reconnect, or net_mqtt_deinit() to release it entirely.
 */
void net_mqtt_disconnect(net_mqtt_t *mqtt);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PUBLISH

/** @name Publish
 * @{ */

/**
 * @brief Stage a message to publish to a topic, waiting for room to do so.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), must be connected - see
 * net_mqtt_connect().
 * @param topic Topic to publish to. Only borrowed - see the note below on
 * how long it (and @p payload) must stay valid.
 * @param payload Message payload. May be NULL if payload_len is 0, for an
 * empty message. Only borrowed, like @p topic.
 * @param payload_len Length of payload in bytes.
 * @param qos Delivery guarantee for this message - see net_mqtt_qos_t.
 * All three levels are implemented.
 * @param retain If true, the broker keeps this message as the topic's
 * last-known value, delivered immediately to any client that subscribes
 * to it afterward - until replaced by another retained publish, or
 * cleared with a retained empty message.
 * @return A message id (never 0) if the message was accepted for
 * sending - not yet sent, see below. `0` if @p mqtt was NULL, @p topic
 * was NULL, or - after waiting, see below - @p mqtt wasn't/isn't
 * connected.
 *
 * This doesn't send anything itself - it stages the message and returns,
 * and net_poll() does the actual write on a later call (see its own
 * doc). That's not just a performance detail: for net_mqtt_qos_1/
 * net_mqtt_qos_2, completion means waiting for a reply (PUBACK, or
 * PUBREC-then-PUBCOMP) that can only arrive interleaved with other
 * traffic on the same connection (an incoming subscribed message, say),
 * which a synchronous call blocking on "read exactly one reply" can't
 * safely do - so net_mqtt_qos_0 goes through the same staged path too,
 * rather than being a special synchronous case. For net_mqtt_qos_1/
 * net_mqtt_qos_2, the message id doesn't count as sent - and the publish
 * slot doesn't free up - until that full reply chain completes (or times
 * out after the handle's own timeout_ms, measured from whichever packet
 * this module sent most recently for it, reported as a
 * net_mqtt_event_error); no retry is attempted on a timeout.
 *
 * Only one outstanding publish at a time for now (a queue is future
 * work) - if one is already staged when this is called, it blocks until
 * net_poll() drains it (freeing the slot for this call to use) or the
 * connection drops, up to the handle's own timeout_ms (`0` returns
 * immediately rather than waiting, same as every other timeout_ms on
 * this handle). This does not busy-wait - a concurrent net_poll()/
 * net_mqtt_disconnect() on another thread/core still makes progress
 * while a call is blocked here.
 *
 * @p topic and @p payload are borrowed, not copied - they must stay
 * valid until net_poll() actually sends this message, signaled by a
 * net_mqtt_event_sent (success) or net_mqtt_event_error (failure) event
 * whose payload carries this same message id. Disconnecting before that
 * happens abandons the staged message silently - neither event fires.
 */
uint32_t net_mqtt_publish(net_mqtt_t *mqtt, const char *topic,
                          const void *payload, size_t payload_len,
                          net_mqtt_qos_t qos, bool retain);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// SUBSCRIBE

/** @name Subscribe
 * @{ */

/**
 * @brief Stage a subscription to a topic filter, waiting for room to do so.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), must be connected.
 * @param topic Topic filter - may include MQTT wildcards (`+` for a
 * single level, `#` as a trailing multi-level match). Copied, not
 * borrowed - unlike net_mqtt_publish()'s @p topic, there's no need to
 * keep this valid after the call returns (it's copied into the
 * subscription table immediately, since a confirmed subscription has to
 * outlive the call either way).
 * @param qos Maximum delivery guarantee requested for this subscription -
 * see net_mqtt_qos_t. The broker may grant a lower QoS than requested,
 * never higher - see net_mqtt_subscribed_t::granted_qos. Only
 * net_mqtt_qos_0 is implemented so far - requesting net_mqtt_qos_1/
 * net_mqtt_qos_2 currently just fails (see @return).
 * @return A message id (never 0) if the request was accepted for
 * sending - not yet sent, see below. `0` if @p mqtt was NULL, @p topic
 * was NULL, @p qos wasn't net_mqtt_qos_0, NET_MQTT_TOPIC_CAPACITY active
 * filters are already in use, or - after waiting, see below - @p mqtt
 * wasn't/isn't connected.
 *
 * Same staged design as net_mqtt_publish(), for the identical reason -
 * see its own doc: this stages the request and returns, net_poll() does
 * the actual write and waits for the broker's SUBACK, and only one
 * outstanding subscribe request is served at a time (blocking here, not
 * failing, if another is already in flight - same rules as
 * net_mqtt_publish()'s own pending-slot wait). Completion (or failure)
 * is reported via a net_mqtt_event_subscribed or net_mqtt_event_error
 * event whose payload carries this same message id.
 *
 * Messages matching this filter arrive - via net_poll() - as
 * net_mqtt_event_received events on whichever callback is currently
 * registered via net_mqtt_set_callback() - register that first, since
 * nothing is queued for a callback that isn't set yet.
 */
uint32_t net_mqtt_subscribe(net_mqtt_t *mqtt, const char *topic,
                            net_mqtt_qos_t qos);

/**
 * @brief Stage removal of a topic filter, waiting for room to do so.
 * @ingroup NetworkMQTT
 * @param mqtt Handle from net_mqtt_init(), must be connected.
 * @param topic Topic filter previously passed to net_mqtt_subscribe() -
 * must match exactly, not just overlap. Copied, not borrowed - same
 * reasoning as net_mqtt_subscribe()'s own @p topic.
 * @return A message id (never 0) if the request was accepted for
 * sending - not yet sent, see below. `0` if @p mqtt was NULL, @p topic
 * was NULL or not currently subscribed (see net_mqtt_subscribe()), or -
 * after waiting, see below - @p mqtt wasn't/isn't connected.
 *
 * Same staged design as net_mqtt_subscribe(), for the identical reason -
 * see its own doc: this stages the request and returns, net_poll() does
 * the actual write and waits for the broker's UNSUBACK, and only one
 * outstanding unsubscribe request is served at a time (blocking here,
 * not failing, if another is already in flight - independent of
 * net_mqtt_subscribe()'s own pending slot, so a subscribe and an
 * unsubscribe for two different filters may be in flight together).
 * Completion (or failure) is reported via a net_mqtt_event_unsubscribed
 * or net_mqtt_event_error event whose payload carries this same message
 * id. The topic filter stops matching new messages only once that event
 * fires, not at the moment this call returns.
 */
uint32_t net_mqtt_unsubscribe(net_mqtt_t *mqtt, const char *topic);

/** @} */

#ifdef __cplusplus
}
#endif

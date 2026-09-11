#pragma once
#include <picofuse/net.h>
#include <picofuse/sys.h>

// Generous enough for "<sys_env_name()>-<sys_env_serial()>" on every
// target this project builds for, without needing to be exact - a
// caller-supplied client_id longer than this is simply truncated (see
// sys_sprintf()'s own truncation semantics).
#define NET_MQTT_CLIENT_ID_SIZE 64

// Credentials (tokens/API keys especially) tend to run longer than a
// client_id - a caller-supplied username/password longer than this is
// simply truncated (see sys_sprintf()'s own truncation semantics).
#define NET_MQTT_CREDENTIAL_SIZE 128


// Default for net_mqtt_config_t::keepalive_s when left 0 - see its own
// doc.
#define _NET_MQTT_KEEPALIVE_S 60

// An incoming PUBLISH payload this size or smaller is read into a stack
// buffer - see _net_mqtt_poll_read_publish()'s own doc on why net_mqtt_
// received_t::payload is fully buffered rather than streamed. Generous
// for typical short messages (sensor readings, small JSON blobs) without
// needing sys_malloc() for the common case.
#define _NET_MQTT_PAYLOAD_STACK_SIZE 256

// A hard ceiling on a single incoming PUBLISH payload - claimed (via its
// Remaining Length) larger than this is rejected outright rather than
// attempting a matching sys_malloc(), which a malicious or corrupt
// broker could otherwise use to push an arbitrarily large allocation (up
// to the protocol's own ~256MB ceiling) onto a resource-constrained
// device.
#define _NET_MQTT_PAYLOAD_MAX_SIZE 8192

// MQTT 3.1.1 fixed-header first byte for each packet type - PUBLISH's
// low nibble also carries DUP/QoS/RETAIN flags (see publish.c). PUBREL's
// low nibble is fixed at 0x2 by the spec itself (not a free choice the
// way PUBLISH's is), so its byte is already complete here - nothing else
// ever needs to OR further flags into it.
#define _NET_MQTT_PACKET_CONNECT 0x10
#define _NET_MQTT_PACKET_CONNACK 0x20
#define _NET_MQTT_PACKET_PUBLISH 0x30
#define _NET_MQTT_PACKET_PUBACK 0x40
#define _NET_MQTT_PACKET_PUBREC 0x50
#define _NET_MQTT_PACKET_PUBREL 0x62
#define _NET_MQTT_PACKET_PUBCOMP 0x70
#define _NET_MQTT_PACKET_SUBSCRIBE 0x82 // Reserved flags fixed at 0x2, like
                                        // PUBREL - see its own doc.
#define _NET_MQTT_PACKET_SUBACK 0x90
#define _NET_MQTT_PACKET_UNSUBSCRIBE 0xA2 // Reserved flags fixed at 0x2,
                                          // like SUBSCRIBE/PUBREL.
#define _NET_MQTT_PACKET_UNSUBACK 0xB0
#define _NET_MQTT_PACKET_PINGREQ 0xC0
#define _NET_MQTT_PACKET_PINGRESP 0xD0
#define _NET_MQTT_PACKET_DISCONNECT 0xE0

// PUBLISH's own QoS bits (bits 2-1 of its fixed header byte, alongside
// DUP/RETAIN - see _NET_MQTT_PACKET_PUBLISH's own doc).
#define _NET_MQTT_PUBLISH_FLAG_QOS1 0x02
#define _NET_MQTT_PUBLISH_FLAG_QOS2 0x04

#define _NET_MQTT_PROTOCOL_LEVEL 0x04 // MQTT 3.1.1
#define _NET_MQTT_CONNECT_FLAG_CLEAN_SESSION 0x02
#define _NET_MQTT_CONNECT_FLAG_USERNAME 0x80
#define _NET_MQTT_CONNECT_FLAG_PASSWORD 0x40

///////////////////////////////////////////////////////////////////////////////
// TYPES

// What (if anything) net_poll() (poll.c) needs to do for this handle's
// outstanding publish - see net_mqtt_publish()'s own doc on why sending
// happens there rather than synchronously inside net_mqtt_publish()
// itself. Only one outstanding publish at a time for now (a queue is
// future work) - net_mqtt_publish() fails (returns 0) if called again
// while this is anything but idle.
typedef enum {
  _net_mqtt_publish_idle,             // Nothing to do.
  _net_mqtt_publish_qos0,             // Send it, then fire
                                      // net_mqtt_event_sent immediately -
                                      // no reply to wait for.
  _net_mqtt_publish_qos1,             // Send it (with a packet id, unlike
                                      // QoS 0) and move to
                                      // qos1_wait_puback - not done until
                                      // that arrives.
  _net_mqtt_publish_qos1_wait_puback, // Sent - waiting for a PUBACK
                                      // whose packet id matches. Still
                                      // occupies the one pending slot;
                                      // see net_mqtt_publish()'s own doc
                                      // on why "sent" means acknowledged,
                                      // not just written.
  _net_mqtt_publish_qos2,             // Send it (same shape as QoS 1's
                                      // own PUBLISH, different QoS bits)
                                      // and move to qos2_wait_pubrec.
  _net_mqtt_publish_qos2_wait_pubrec, // Sent - waiting for a PUBREC whose
                                      // packet id matches. On match, sends
                                      // PUBREL and moves to
                                      // qos2_wait_pubcomp - not done yet.
  _net_mqtt_publish_qos2_wait_pubcomp, // PUBREL sent - waiting for a
                                       // PUBCOMP whose packet id matches.
                                       // *That's* what finally completes
                                       // a QoS 2 publish.
} _net_mqtt_publish_state_t;

// Staged by net_mqtt_publish(), consumed by net_poll() - see
// _net_mqtt_publish_state_t's own doc.
typedef struct {
  _net_mqtt_publish_state_t state;
  const char *topic;   // Borrowed from the caller - see net_mqtt_publish()'s
                       // own doc on how long it must stay valid.
  const void *payload; // Borrowed likewise. NULL/0 for none.
  size_t payload_len;
  bool retain;
  uint32_t message_id; // Already allocated at stage time, not send time -
                       // it's what net_mqtt_publish() itself returns.
  uint16_t packet_id;  // QoS 1/2 only (0 - never a real one, see
                       // net_mqtt_t::next_packet_id - for QoS 0, which
                       // has no packet id on the wire at all).
  uint64_t sent_at_ms; // QoS 1/2 only - sys_timestamp_ms() when the
                       // PUBLISH itself went out, so net_poll() can
                       // notice a PUBACK that never arrives within
                       // timeout_ms of it.
  uint16_t last_timed_out_packet_id; // 0 (never a real packet id - see
                       // net_mqtt_t::next_packet_id's own doc) if the
                       // last publish didn't time out, or its packet_id
                       // if it did - see
                       // _net_mqtt_poll_publish_check_timeout()'s own
                       // doc on why: a PUBACK/PUBREC/PUBCOMP that turns
                       // up late, after the timeout already gave up and
                       // freed the slot for a new publish, is matched
                       // against this instead of asserting against
                       // whatever's using the slot now, and quietly
                       // discarded rather than either crashing or being
                       // misread as a reply to something else entirely.
} _net_mqtt_publish_pending_t;

// What (if anything) net_poll() (poll.c) needs to do for this handle's
// outstanding subscribe request - see net_mqtt_subscribe()'s own doc on
// why sending happens there, same reasoning as publish's own state
// machine (_net_mqtt_publish_state_t). Only one outstanding subscribe
// request at a time, same as publish.
typedef enum {
  _net_mqtt_subscribe_idle,       // Nothing to do.
  _net_mqtt_subscribe_requesting, // Send SUBSCRIBE (with a packet id) and
                                  // move to wait_suback.
  _net_mqtt_subscribe_wait_suback, // Sent - waiting for a SUBACK whose
                                   // packet id matches, carrying the
                                   // broker's granted QoS.
} _net_mqtt_subscribe_state_t;

// One confirmed (SUBACK'd) subscription - see net_mqtt_t::topics's own
// doc. Unlike _net_mqtt_subscribe_pending_t below (the in-flight
// handshake, one at a time), this is the long-lived table of everything
// currently subscribed.
typedef struct {
  bool active;
  char filter[NET_MQTT_TOPIC_FILTER_SIZE];
  net_mqtt_qos_t granted_qos;
} _net_mqtt_topic_t;

// Staged by net_mqtt_subscribe(), consumed by net_poll() - see
// _net_mqtt_subscribe_state_t's own doc.
typedef struct {
  _net_mqtt_subscribe_state_t state;
  char filter[NET_MQTT_TOPIC_FILTER_SIZE]; // Copied at stage time - see
                                           // net_mqtt_subscribe()'s own
                                           // doc on why (unlike publish's
                                           // borrowed topic).
  net_mqtt_qos_t requested_qos;
  uint32_t message_id; // Already allocated at stage time - see
                       // net_mqtt_publish_pending_t::message_id's own
                       // doc for the identical reasoning.
  uint16_t packet_id;  // Shares net_mqtt_t::next_packet_id's counter with
                       // publish - packet ids are one namespace for the
                       // whole connection, not per-operation-kind.
  uint64_t sent_at_ms; // sys_timestamp_ms() when SUBSCRIBE went out - see
                       // _net_mqtt_publish_pending_t::sent_at_ms's own
                       // doc for the identical reasoning.
  _net_mqtt_topic_t *topic; // Slot in net_mqtt_t::topics reserved by
                       // _net_mqtt_topic_alloc() at stage time (NULL if
                       // none/idle) - poll.c's future SUBACK handling
                       // fills it in (or, on denial/failure,
                       // _net_mqtt_topic_free()s it back) rather than
                       // needing to find a slot itself at confirm time.
  uint16_t last_timed_out_packet_id; // Same tombstone as
                       // _net_mqtt_publish_pending_t's own field - see
                       // its doc. The reserved topics[] slot is already
                       // freed by the time this is set (see
                       // _net_mqtt_poll_subscribe_check_timeout()'s own
                       // doc), so a late SUBACK matching this is just
                       // read and discarded, nothing left to fill in.
} _net_mqtt_subscribe_pending_t;

// What (if anything) net_poll() (poll.c) needs to do for this handle's
// outstanding unsubscribe request - independent of
// _net_mqtt_subscribe_state_t's own slot (a subscribe and an unsubscribe
// for two different filters may be in flight together, since they don't
// contend for the same pending state or packet id), but only one
// outstanding unsubscribe request at a time, same reasoning as publish/
// subscribe.
typedef enum {
  _net_mqtt_unsubscribe_idle,       // Nothing to do.
  _net_mqtt_unsubscribe_requesting, // Send UNSUBSCRIBE (with a packet id)
                                    // and move to wait_unsuback.
  _net_mqtt_unsubscribe_wait_unsuback, // Sent - waiting for an UNSUBACK
                                       // whose packet id matches.
} _net_mqtt_unsubscribe_state_t;

// Staged by net_mqtt_unsubscribe(), consumed by net_poll() - see
// _net_mqtt_unsubscribe_state_t's own doc.
typedef struct {
  _net_mqtt_unsubscribe_state_t state;
  char filter[NET_MQTT_TOPIC_FILTER_SIZE]; // Copied at stage time - same
                                           // reasoning as
                                           // _net_mqtt_subscribe_pending_t::filter.
  uint32_t message_id; // Already allocated at stage time - see
                       // _net_mqtt_publish_pending_t::message_id's own
                       // doc for the identical reasoning.
  uint16_t packet_id;  // Shares net_mqtt_t::next_packet_id's counter -
                       // see _net_mqtt_subscribe_pending_t::packet_id's
                       // own doc.
  uint64_t sent_at_ms; // sys_timestamp_ms() when UNSUBSCRIBE went out -
                       // see _net_mqtt_publish_pending_t::sent_at_ms's
                       // own doc for the identical reasoning.
  _net_mqtt_topic_t *topic; // The confirmed net_mqtt_t::topics entry
                            // this is removing - looked up by filter at
                            // stage time (net_mqtt_unsubscribe() fails if
                            // not found), so poll.c doesn't need to
                            // re-search topics[] at confirm time. Unlike
                            // _net_mqtt_subscribe_pending_t::topic, this
                            // slot isn't freed (active set back to
                            // false) until UNSUBACK actually confirms -
                            // the subscription (and whatever messages it
                            // may still deliver) stays live until then.
  uint16_t last_timed_out_packet_id; // Same tombstone as
                       // _net_mqtt_publish_pending_t's own field - see
                       // its doc. A late UNSUBACK matching this is just
                       // discarded, same as publish/subscribe's own -
                       // the target topic is left exactly as
                       // _net_mqtt_poll_unsubscribe_check_timeout() left
                       // it (still active/subscribed, as far as this
                       // client can tell) rather than retroactively
                       // updated, since by the time a late reply could
                       // arrive this field may already have been reused
                       // by a newer, unrelated net_mqtt_unsubscribe()
                       // call for a different filter.
} _net_mqtt_unsubscribe_pending_t;

// A singleton, not a pool - see net_mqtt_t's own doc on why.
struct net_mqtt_t {
  net_addr_t addr;
  uint16_t port;
  uint32_t timeout_ms;
  uint16_t keepalive_s; // Resolved from net_mqtt_config_t::keepalive_s at
                        // init time (never 0 after that - see
                        // net_mqtt_init()'s own doc) - both sent in
                        // CONNECT and used to schedule this client's own
                        // automatic PINGREQ keepalives (see poll.c's
                        // _net_mqtt_poll_ping_send()).
  char client_id[NET_MQTT_CLIENT_ID_SIZE];
  char username[NET_MQTT_CREDENTIAL_SIZE]; // Empty string - not "" vs.
                                           // NULL - means "not set".
  char password[NET_MQTT_CREDENTIAL_SIZE];
  net_mqtt_event_callback_t callback;
  void *userdata;
  bool active;
  bool connected;
  sys_iostream_t *conn; // Only valid while connected.
  sys_mutex_t *lock;    // Guards conn/connected and every write to the
                        // socket - see connect.c/publish.c's own lock
                        // discipline (held for the I/O, released before
                        // any event fires, so a callback that calls back
                        // into this module doesn't deadlock on it).
                        // Created once, lazily, and never destroyed - see
                        // mqtt.c's own note on why tying its lifetime to
                        // individual init()/deinit() cycles would race a
                        // blocked net_mqtt_publish() (see publish_cond
                        // below) reacquiring it against deinit()
                        // destroying it out from under that reacquire.
  sys_cond_t *publish_cond; // Signaled whenever `publish.state` becomes
                            // idle again or `connected` becomes false -
                            // what a blocked net_mqtt_publish() call
                            // waits on. Same lifetime note as lock above.
  uint32_t next_message_id; // Guarded by lock - see net_mqtt_publish()'s
                            // own doc on why it returns one of these. 0
                            // is skipped on wraparound, since it's
                            // reserved for "failure".
  uint16_t next_packet_id; // Guarded by lock - the wire-level Packet
                           // Identifier for QoS 1/2 (distinct from
                           // next_message_id, which is purely our own
                           // client-side bookkeeping and never appears
                           // on the wire). 0 is skipped on wraparound -
                           // the protocol itself reserves it, unlike
                           // next_message_id where 0 is just our own
                           // "failure" sentinel.
  bool ping_outstanding; // True from the moment an automatic PINGREQ is
                        // sent until either a PINGRESP arrives or
                        // ping_sent_at_ms's own timeout gives up on it
                        // (see poll.c's _net_mqtt_poll_ping_send()/
                        // _net_mqtt_poll_ping_check_timeout()). Only one
                        // outstanding at a time, same reasoning as
                        // publish/subscribe/unsubscribe's own one-at-a-
                        // time slots - simpler here still, since there's
                        // nothing to distinguish a second one from.
  uint64_t ping_sent_at_ms; // Dual purpose: while !ping_outstanding, when
                            // the last PINGREQ went out (or connected,
                            // whichever's more recent) - used to decide
                            // when the next one is due. While
                            // ping_outstanding, when *this* one went out
                            // - used to notice a PINGRESP that never
                            // arrives within timeout_ms (treated as the
                            // whole connection being dead, not just one
                            // operation - see
                            // _net_mqtt_poll_ping_check_timeout()'s own
                            // doc on why that's different from every
                            // other check_timeout in this module).
  _net_mqtt_publish_pending_t publish; // Guarded by lock.
  _net_mqtt_subscribe_pending_t subscribe; // Guarded by lock.
  _net_mqtt_unsubscribe_pending_t unsubscribe; // Guarded by lock.
  _net_mqtt_topic_t topics[NET_MQTT_TOPIC_CAPACITY]; // Guarded by lock -
                                                     // confirmed
                                                     // subscriptions only;
                                                     // see
                                                     // _net_mqtt_topic_t's
                                                     // own doc.
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Defined in mqtt.c - every other file in this module reaches the one
// handle through this rather than its own copy.
extern struct net_mqtt_t _net_mqtt_singleton;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Delivers @p event to @p mqtt's registered callback, if any - a
 * no-op (not an error) if none is currently set. */
void _net_mqtt_fire_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event);

/** @brief Tears down @p mqtt's connection after a mid-packet I/O failure
 * (a partial write/read leaves the connection's framing unrecoverable,
 * so it can't just be left looking usable) - closes and clears conn/
 * connected, abandons any staged/in-flight publish, subscribe, or
 * unsubscribe request (see net_mqtt_publish()/net_mqtt_subscribe()/
 * net_mqtt_unsubscribe()'s own doc on that being a silent abandonment),
 * forgets every confirmed subscription in @p mqtt's topics - once the
 * connection is gone, for any reason, none of them are still being
 * delivered to regardless (see net_mqtt_disconnect()'s own doc on why
 * that's true even for a clean disconnect, not just an unexpected drop)
 * - and clears ping_outstanding (nothing left to get a PINGRESP for).
 * Caller must already hold `lock` and must fire a
 * net_mqtt_event_disconnected event itself once it's released - this
 * doesn't fire one on its own, since it never unlocks (see this module's
 * lock discipline: never fire an event while holding the lock). A no-op
 * if not currently connected. */
void _net_mqtt_abort_connection_locked(net_mqtt_t *mqtt);

/** @brief Allocates the next client-side message id for @p mqtt,
 * skipping 0 on wraparound - see net_mqtt_t::next_message_id's own doc
 * on why (0 is our own "failure" sentinel, not a protocol requirement
 * the way packet ids are). Shared by net_mqtt_publish() and
 * net_mqtt_subscribe(). */
uint32_t _net_mqtt_next_message_id(net_mqtt_t *mqtt);

/** @brief Allocates the next wire-level Packet Identifier for @p mqtt,
 * skipping 0 - see net_mqtt_t::next_packet_id's own doc on why (the
 * protocol itself reserves 0, not just a "failure" convention of our
 * own). Shared by net_mqtt_publish() and net_mqtt_subscribe() - packet
 * ids are one namespace for the whole connection. */
uint16_t _net_mqtt_next_packet_id(net_mqtt_t *mqtt);

/** @brief Releases @p topic - a slot _net_mqtt_topic_alloc()
 * (subscribe.c) reserved in @p mqtt's topics - making it available again.
 * A no-op for a NULL @p topic, so callers that may or may not have
 * actually allocated one don't each need their own guard (see
 * _net_mqtt_subscribe_pending_t::topic's own doc). Caller must already
 * hold `lock`. */
void _net_mqtt_topic_free(net_mqtt_t *mqtt, _net_mqtt_topic_t *topic);

/** @brief Writes exactly @p n bytes to @p conn, retrying short writes
 * until either the whole write completes or @p timeout_ms elapses.
 * @return false on a short write/timeout/NULL @p conn. */
bool _net_mqtt_write_exact(sys_iostream_t *conn, const void *data, size_t n,
                           uint32_t timeout_ms);

/** @brief Reads exactly @p n bytes from @p conn, retrying short reads
 * until either @p n bytes have arrived or @p timeout_ms elapses.
 * @return false on a short read/timeout/NULL @p conn. */
bool _net_mqtt_read_exact(sys_iostream_t *conn, void *data, size_t n,
                          uint32_t timeout_ms);

/** @brief Encodes @p value using MQTT's variable-length "Remaining
 * Length" scheme (7 bits per byte, continuation bit set on every byte
 * but the last) into @p buf.
 * @return Bytes written (1-4), or 0 if @p value doesn't fit in 4 bytes
 * (> 268,435,455, the protocol's own ceiling) or @p buf_size is too
 * small. */
size_t _net_mqtt_encode_length(uint32_t value, uint8_t *buf, size_t buf_size);

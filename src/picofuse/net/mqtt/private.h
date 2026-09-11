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

// Fixed internally
#define _NET_MQTT_KEEPALIVE_S 60

// MQTT 3.1.1 fixed-header first byte for each packet type - PUBLISH's
// low nibble also carries DUP/QoS/RETAIN flags (see publish.c), unlike
// the other two, which are always sent with flags 0.
#define _NET_MQTT_PACKET_CONNECT 0x10
#define _NET_MQTT_PACKET_CONNACK 0x20
#define _NET_MQTT_PACKET_PUBLISH 0x30
#define _NET_MQTT_PACKET_PUBACK 0x40
#define _NET_MQTT_PACKET_DISCONNECT 0xE0

// PUBLISH's own QoS bits (bits 2-1 of its fixed header byte, alongside
// DUP/RETAIN - see _NET_MQTT_PACKET_PUBLISH's own doc).
#define _NET_MQTT_PUBLISH_FLAG_QOS1 0x02

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
  _net_mqtt_publish_idle,            // Nothing to do.
  _net_mqtt_publish_qos0,            // Send it, then fire
                                     // net_mqtt_event_sent immediately -
                                     // no reply to wait for.
  _net_mqtt_publish_qos1,            // Send it (with a packet id, unlike
                                     // QoS 0) and move to
                                     // qos1_wait_puback - not done until
                                     // that arrives.
  _net_mqtt_publish_qos1_wait_puback, // Sent - waiting for a PUBACK
                                      // whose packet id matches. Still
                                      // occupies the one pending slot;
                                      // see net_mqtt_publish()'s own doc
                                      // on why "sent" means acknowledged,
                                      // not just written.
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
} _net_mqtt_publish_pending_t;

// A singleton, not a pool - see net_mqtt_t's own doc on why.
struct net_mqtt_t {
  net_addr_t addr;
  uint16_t port;
  uint32_t timeout_ms;
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
  _net_mqtt_publish_pending_t publish; // Guarded by lock.
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Defined in mqtt.c - every other file in this module reaches the one
// handle through this rather than its own copy.
extern struct net_mqtt_t _net_mqtt_singleton;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Delivers @p event to the singleton's registered callback, if
 * any - a no-op (not an error) if none is currently set. */
void _net_mqtt_fire_event(const net_mqtt_event_t *event);

/** @brief Tears down the connection after a mid-packet I/O failure (a
 * partial write/read leaves the connection's framing unrecoverable, so
 * it can't just be left looking usable) - closes and clears conn/
 * connected. Caller must already hold `lock` and must fire a
 * net_mqtt_event_disconnected event itself once it's released - this
 * doesn't fire one on its own, since it never unlocks (see this module's
 * lock discipline: never fire an event while holding the lock). A no-op
 * if not currently connected. */
void _net_mqtt_abort_connection_locked(void);

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

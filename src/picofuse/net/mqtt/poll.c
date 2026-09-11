#include "private.h"
#include <string.h>

// The small per-publish state machine this module drives - see
// _net_mqtt_publish_state_t's own doc for the states themselves.
// Incoming replies are read via _net_mqtt_poll_read_dispatch() below,
// which peeks the fixed header's type nibble first and routes to the
// matching per-type handler - each handler then sys_assert()s that our
// own state machine agrees it should be expecting exactly that type,
// rather than independently peeking and guessing "is this mine?" the way
// this file used to. That guess-per-handler pattern was only safe while
// PUBACK/PUBREC/PUBCOMP shared one state machine with nothing else ever
// arriving unprompted; SUBACK (net_mqtt_subscribe(), still to come) and
// eventually unprompted PUBLISH delivery break that assumption, since
// more than one kind of reply can then be legitimately in flight - the
// dispatcher is what makes routing those safe.
//
// _net_mqtt_poll() (bottom of this file) is the only place in this
// module that touches _net_mqtt_singleton directly - it's the root entry
// from net_poll() (no caller-supplied handle exists there to use
// instead, same reasoning as net_mqtt_init()'s own doc), and every
// helper below it takes the resulting net_mqtt_t* as a parameter rather
// than reaching for the singleton itself.

// Fires net_mqtt_event_error/net_mqtt_event_disconnected for a broken
// connection - the common tail of both a mid-packet write failure and a
// malformed/unexpected reply. Caller must NOT be holding the lock (both
// events fire from here, and this module never fires one while locked).
static void _net_mqtt_publish_fail(net_mqtt_t *mqtt, uint32_t message_id,
                                   const char *message) {
  net_mqtt_event_t error_event = {
      .type = net_mqtt_event_error,
      .data.error = {.message = message, .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &error_event);
  net_mqtt_event_t disconnected_event = {.type = net_mqtt_event_disconnected};
  _net_mqtt_fire_event(mqtt, &disconnected_event);
}

// Sends whatever net_mqtt_publish() has staged (QoS 0, 1, or 2) - see
// net_mqtt_publish()'s own doc on why sending happens here rather than
// synchronously in that call.
static bool _net_mqtt_poll_publish_send(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  _net_mqtt_publish_state_t state = mqtt->publish.state;
  if (state != _net_mqtt_publish_qos0 && state != _net_mqtt_publish_qos1 &&
      state != _net_mqtt_publish_qos2) {
    sys_mutex_unlock(mqtt->lock);
    return false; // Nothing staged to send right now.
  }

  // Snapshot before the lock is released below - topic/payload are the
  // caller's own borrowed pointers (see net_mqtt_publish()'s own doc),
  // still needed after unlocking to build an event.
  const char *topic = mqtt->publish.topic;
  const void *payload = mqtt->publish.payload;
  size_t payload_len = mqtt->publish.payload_len;
  bool retain = mqtt->publish.retain;
  uint32_t message_id = mqtt->publish.message_id;
  uint16_t packet_id = mqtt->publish.packet_id;

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;

  bool is_qos1 = state == _net_mqtt_publish_qos1;
  bool is_qos2 = state == _net_mqtt_publish_qos2;
  bool needs_packet_id = is_qos1 || is_qos2;

  // Fixed header: packet type plus DUP/QoS/RETAIN flags in the low
  // nibble - DUP always 0 (no retransmission tracking), QoS bits from
  // the staged QoS (00/01/10), RETAIN from the staged parameter.
  // Variable header: Topic Name, THEN - QoS 1/2 only - a 2-byte Packet
  // Identifier (that order matters - it comes after the topic, not
  // before it). Payload: the raw message bytes, un-prefixed (length
  // implied by Remaining Length minus everything before it).
  size_t topic_len = strlen(topic);
  uint32_t remaining_length = 2 + (uint32_t)topic_len +
                              (needs_packet_id ? 2u : 0u) +
                              (uint32_t)payload_len;

  uint8_t header[1 + 4 + 2];
  size_t pos = 0;
  uint8_t qos_flags = is_qos1   ? _NET_MQTT_PUBLISH_FLAG_QOS1
                      : is_qos2 ? _NET_MQTT_PUBLISH_FLAG_QOS2
                                : 0;
  header[pos++] =
      (uint8_t)(_NET_MQTT_PACKET_PUBLISH | qos_flags | (retain ? 0x01 : 0x00));

  size_t len_n = _net_mqtt_encode_length(remaining_length, header + pos,
                                         sizeof(header) - pos);
  bool ok = len_n != 0;
  if (ok) {
    pos += len_n;
    header[pos++] = (uint8_t)(topic_len >> 8);
    header[pos++] = (uint8_t)(topic_len & 0xFF);

    uint8_t packet_id_bytes[2] = {(uint8_t)(packet_id >> 8),
                                  (uint8_t)(packet_id & 0xFF)};

    // Written as separate calls rather than copied into one combined
    // buffer first - payload_len has no protocol ceiling (see
    // net_mqtt_publish()'s own doc), so there's no size that's always
    // safe to bundle on the stack the way CONNECT's small, fixed-shape
    // packet could be.
    ok = _net_mqtt_write_exact(conn, header, pos, timeout_ms) &&
         _net_mqtt_write_exact(conn, topic, topic_len, timeout_ms) &&
         (!needs_packet_id ||
          _net_mqtt_write_exact(conn, packet_id_bytes, sizeof(packet_id_bytes),
                                timeout_ms)) &&
         (payload_len == 0 ||
          _net_mqtt_write_exact(conn, payload, payload_len, timeout_ms));
  }

  if (!ok) {
    // A partial write here leaves a half-sent packet on the wire - the
    // connection's framing can't be trusted after that, so it's torn
    // down rather than left looking usable for the next call.
    _net_mqtt_abort_connection_locked(mqtt); // also resets publish.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_publish_fail(mqtt, message_id, "PUBLISH write failed");
    return true;
  }

  if (is_qos1 || is_qos2) {
    // Not done yet - stays occupied until the matching reply chain
    // completes (or times out) - see _net_mqtt_publish_qos1_wait_puback/
    // _net_mqtt_publish_qos2_wait_pubrec's own doc on why "sent" means
    // acknowledged, not just written. No event fires yet, and the slot
    // isn't freed (no broadcast) either.
    mqtt->publish.state = is_qos1 ? _net_mqtt_publish_qos1_wait_puback
                                  : _net_mqtt_publish_qos2_wait_pubrec;
    mqtt->publish.sent_at_ms = sys_timestamp_ms();
    sys_mutex_unlock(mqtt->lock);
    return true;
  }

  mqtt->publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &sent_event);
  return true;
}

// Reads+validates a fixed 4-byte "simple ack" packet - the shape PUBACK,
// PUBREC, and PUBCOMP all share: fixed header, Remaining Length always
// 2, a 2-byte packet id, no payload. So this reads it as one known-size
// block rather than needing a general variable-length packet parser
// (that's for a future PUBLISH/SUBACK reader). Caller must already hold
// the lock and must already have confirmed (via
// _net_mqtt_poll_read_dispatch()'s own peek) that a packet of
// expected_type is actually waiting - this only validates its shape and
// packet id, it doesn't check presence. Purely a wire-level helper - no
// net_mqtt_t needed.
static bool _net_mqtt_poll_read_ack(sys_iostream_t *conn, uint32_t timeout_ms,
                                    uint8_t expected_type,
                                    uint16_t expected_packet_id) {
  uint8_t buf[4];
  if (!_net_mqtt_read_exact(conn, buf, sizeof(buf), timeout_ms)) {
    return false;
  }
  uint16_t got_packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
  return buf[0] == expected_type && buf[1] == 0x02 &&
         got_packet_id == expected_packet_id;
}

// Reads the PUBACK for the QoS 1 publish currently awaited - completes
// it (fires net_mqtt_event_sent) on a match. Caller (the dispatcher)
// already holds the lock and has already peeked a PUBACK type byte.
static bool _net_mqtt_poll_publish_read_puback(net_mqtt_t *mqtt) {
  // Anything but qos1_wait_puback here means the dispatcher routed a
  // PUBACK to a state machine that wasn't expecting one - a genuine
  // internal-consistency bug (or a stray/duplicate PUBACK from the
  // broker after we'd already given up on it - see
  // _net_mqtt_poll_publish_check_timeout()'s own doc), not something to
  // silently paper over.
  sys_assert(mqtt->publish.state == _net_mqtt_publish_qos1_wait_puback);

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->publish.message_id;
  uint16_t packet_id = mqtt->publish.packet_id;

  if (!_net_mqtt_poll_read_ack(conn, timeout_ms, _NET_MQTT_PACKET_PUBACK,
                               packet_id)) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets publish.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_publish_fail(mqtt, message_id, "malformed or unexpected PUBACK");
    return true;
  }

  const char *topic = mqtt->publish.topic;
  mqtt->publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &sent_event);
  return true;
}

// Reads the PUBREC for the QoS 2 publish currently awaited. On a match,
// immediately sends PUBREL - a small, fixed-shape packet, much like
// DISCONNECT - and moves to qos2_wait_pubcomp: *not* done yet, see that
// state's own doc on why. Caller (the dispatcher) already holds the lock
// and has already peeked a PUBREC type byte.
static bool _net_mqtt_poll_publish_read_pubrec(net_mqtt_t *mqtt) {
  sys_assert(mqtt->publish.state == _net_mqtt_publish_qos2_wait_pubrec);

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->publish.message_id;
  uint16_t packet_id = mqtt->publish.packet_id;

  if (!_net_mqtt_poll_read_ack(conn, timeout_ms, _NET_MQTT_PACKET_PUBREC,
                               packet_id)) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets publish.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_publish_fail(mqtt, message_id, "malformed or unexpected PUBREC");
    return true;
  }

  // PUBREL: fixed header (type + spec-mandated reserved flags, already
  // complete in _NET_MQTT_PACKET_PUBREL - see its own doc), Remaining
  // Length always 2, then the same packet id PUBREC just confirmed.
  uint8_t pubrel[4] = {_NET_MQTT_PACKET_PUBREL, 0x02, (uint8_t)(packet_id >> 8),
                       (uint8_t)(packet_id & 0xFF)};
  if (!_net_mqtt_write_exact(conn, pubrel, sizeof(pubrel), timeout_ms)) {
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_publish_fail(mqtt, message_id, "PUBREL write failed");
    return true;
  }

  mqtt->publish.state = _net_mqtt_publish_qos2_wait_pubcomp;
  mqtt->publish.sent_at_ms = sys_timestamp_ms();
  sys_mutex_unlock(mqtt->lock);
  return true;
}

// Reads the PUBCOMP for the QoS 2 publish currently awaited - the final
// leg of the chain; a match here is what actually completes a QoS 2
// publish (fires net_mqtt_event_sent). Caller (the dispatcher) already
// holds the lock and has already peeked a PUBCOMP type byte.
static bool _net_mqtt_poll_publish_read_pubcomp(net_mqtt_t *mqtt) {
  sys_assert(mqtt->publish.state == _net_mqtt_publish_qos2_wait_pubcomp);

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->publish.message_id;
  uint16_t packet_id = mqtt->publish.packet_id;

  if (!_net_mqtt_poll_read_ack(conn, timeout_ms, _NET_MQTT_PACKET_PUBCOMP,
                               packet_id)) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets publish.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_publish_fail(mqtt, message_id, "malformed or unexpected PUBCOMP");
    return true;
  }

  const char *topic = mqtt->publish.topic;
  mqtt->publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &sent_event);
  return true;
}

// Peeks whatever incoming packet is waiting, if any, and routes it to
// the handler for its fixed-header type - see this file's own top-of-
// file note on why routing by type up front (rather than each handler
// independently peeking and guessing "is this mine?") is what makes
// more than one kind of unprompted reply safe to have in flight at
// once. sys_iostream_peek() is non-destructive, so a handler that goes
// on to read the packet still sees its type byte as the first byte of
// the stream.
static bool _net_mqtt_poll_read_dispatch(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  sys_iostream_t *conn = mqtt->conn;
  int peeked = sys_iostream_peek(conn);
  if (peeked < 0) {
    sys_mutex_unlock(mqtt->lock);
    return false; // Nothing's arrived yet.
  }

  // Only the fixed header's high nibble identifies the packet type - the
  // low nibble carries per-type flags (DUP/QoS/RETAIN for PUBLISH,
  // spec-fixed reserved bits elsewhere - see _NET_MQTT_PACKET_PUBREL's
  // own doc).
  uint8_t type = (uint8_t)(peeked & 0xF0);

  switch (type) {
  case _NET_MQTT_PACKET_PUBACK:
    return _net_mqtt_poll_publish_read_puback(mqtt); // unlocks itself
  case _NET_MQTT_PACKET_PUBREC:
    return _net_mqtt_poll_publish_read_pubrec(mqtt); // unlocks itself
  case _NET_MQTT_PACKET_PUBCOMP:
    return _net_mqtt_poll_publish_read_pubcomp(mqtt); // unlocks itself
  default:
    // SUBACK/PUBLISH/PINGRESP reading isn't implemented yet (see this
    // file's own top-of-file note). Either way there's nowhere for this
    // packet to go - and it can't just be left on the wire for a future
    // poll() to reconsider, since that would desync framing for
    // whatever's read next - so for now it's treated the same as a
    // malformed reply from an operation that was actually pending.
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_publish_fail(mqtt, 0,
                           "unexpected or not-yet-supported packet type");
    return true;
  }
}

// Fails (rather than hangs forever on) a QoS 1/2 publish whose next
// expected reply never arrived within timeout_ms of the last packet this
// module sent for it (the original PUBLISH, or - for QoS 2's second leg
// - the PUBREL, whichever is most recent: see sent_at_ms's own doc). No
// retry (the spec's own answer - resend with DUP=1, or for QoS 2 past
// PUBREC, just resend PUBREL) yet; just frees the slot and reports the
// failure. Doesn't tear down the connection - one slow/lost reply isn't
// necessarily a dead connection, so future publishes still get to try
// their own luck. Note this means a reply that turns up *after* this
// fires will still be sitting on the wire for the next
// _net_mqtt_poll_read_dispatch() call - which, having nothing pending
// to match it against, will trip that dispatcher's own sys_assert().
static bool _net_mqtt_poll_publish_check_timeout(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  _net_mqtt_publish_state_t state = mqtt->publish.state;
  bool awaiting_reply = state == _net_mqtt_publish_qos1_wait_puback ||
                        state == _net_mqtt_publish_qos2_wait_pubrec ||
                        state == _net_mqtt_publish_qos2_wait_pubcomp;

  if (!awaiting_reply ||
      sys_timestamp_ms() - mqtt->publish.sent_at_ms < mqtt->timeout_ms) {
    sys_mutex_unlock(mqtt->lock);
    return false;
  }

  uint32_t message_id = mqtt->publish.message_id;
  mqtt->publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t error_event = {
      .type = net_mqtt_event_error,
      .data.error = {.message = "acknowledgment timed out",
                     .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &error_event);
  return true;
}

bool _net_mqtt_poll(void) {
  net_mqtt_t *mqtt = &_net_mqtt_singleton;
  if (!mqtt->active || !mqtt->connected) {
    return false;
  }
  // Stops at the first of these that actually does something, rather
  // than always running all three - each can involve blocking I/O
  // so servicing more than one per call would tie up the
  // caller for their combined latency instead of spreading the work
  // across separate net_poll() calls.
  if (_net_mqtt_poll_publish_send(mqtt)) {
    return true;
  }
  if (_net_mqtt_poll_read_dispatch(mqtt)) {
    return true;
  }
  return _net_mqtt_poll_publish_check_timeout(mqtt);
}

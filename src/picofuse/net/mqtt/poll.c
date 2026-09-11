#include "private.h"
#include <string.h>

// The small per-publish state machine this module drives - see
// _net_mqtt_publish_state_t's own doc for the states themselves. A
// general incoming-packet reader for anything beyond
// PUBACK/PUBREC/PUBCOMP (incoming PUBLISH dispatch for
// net_mqtt_subscribe(), PINGRESP for the keep-alive) still needs adding
// here too, alongside those features - _net_mqtt_poll_read_ack() is the
// narrow, three-packet-shapes-only version of what that will eventually
// become.

// Fires net_mqtt_event_error/net_mqtt_event_disconnected for a broken
// connection - the common tail of both a mid-packet write failure and a
// malformed/unexpected reply. Caller must NOT be holding the lock (both
// events fire from here, and this module never fires one while locked).
static void _net_mqtt_publish_fail(uint32_t message_id, const char *message) {
  net_mqtt_event_t error_event = {
      .type = net_mqtt_event_error,
      .data.error = {.message = message, .message_id = message_id}};
  _net_mqtt_fire_event(&error_event);
  net_mqtt_event_t disconnected_event = {.type = net_mqtt_event_disconnected};
  _net_mqtt_fire_event(&disconnected_event);
}

// Sends whatever net_mqtt_publish() has staged (QoS 0, 1, or 2) - see
// net_mqtt_publish()'s own doc on why sending happens here rather than
// synchronously in that call.
static bool _net_mqtt_poll_publish_send(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  _net_mqtt_publish_state_t state = _net_mqtt_singleton.publish.state;
  if (state != _net_mqtt_publish_qos0 && state != _net_mqtt_publish_qos1 &&
      state != _net_mqtt_publish_qos2) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false; // Nothing staged to send right now.
  }

  // Snapshot before the lock is released below - topic/payload are the
  // caller's own borrowed pointers (see net_mqtt_publish()'s own doc),
  // still needed after unlocking to build an event.
  const char *topic = _net_mqtt_singleton.publish.topic;
  const void *payload = _net_mqtt_singleton.publish.payload;
  size_t payload_len = _net_mqtt_singleton.publish.payload_len;
  bool retain = _net_mqtt_singleton.publish.retain;
  uint32_t message_id = _net_mqtt_singleton.publish.message_id;
  uint16_t packet_id = _net_mqtt_singleton.publish.packet_id;

  sys_iostream_t *conn = _net_mqtt_singleton.conn;
  uint32_t timeout_ms = _net_mqtt_singleton.timeout_ms;

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
  uint8_t qos_flags = is_qos1  ? _NET_MQTT_PUBLISH_FLAG_QOS1
                     : is_qos2 ? _NET_MQTT_PUBLISH_FLAG_QOS2
                               : 0;
  header[pos++] = (uint8_t)(_NET_MQTT_PACKET_PUBLISH | qos_flags |
                            (retain ? 0x01 : 0x00));

  size_t len_n =
      _net_mqtt_encode_length(remaining_length, header + pos, sizeof(header) - pos);
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
    _net_mqtt_abort_connection_locked(); // also resets publish.state
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    _net_mqtt_publish_fail(message_id, "PUBLISH write failed");
    return true;
  }

  if (is_qos1 || is_qos2) {
    // Not done yet - stays occupied until the matching reply chain
    // completes (or times out) - see _net_mqtt_publish_qos1_wait_puback/
    // _net_mqtt_publish_qos2_wait_pubrec's own doc on why "sent" means
    // acknowledged, not just written. No event fires yet, and the slot
    // isn't freed (no broadcast) either.
    _net_mqtt_singleton.publish.state = is_qos1 ? _net_mqtt_publish_qos1_wait_puback
                                                : _net_mqtt_publish_qos2_wait_pubrec;
    _net_mqtt_singleton.publish.sent_at_ms = sys_timestamp_ms();
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return true;
  }

  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(_net_mqtt_singleton.publish_cond);
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = message_id}};
  _net_mqtt_fire_event(&sent_event);
  return true;
}

// Outcome of _net_mqtt_poll_read_ack() below.
typedef enum {
  _net_mqtt_ack_none, // Nothing's arrived yet - try again next poll().
  _net_mqtt_ack_ok,   // A valid packet of the expected type/id arrived.
  _net_mqtt_ack_bad,  // Something arrived, but wasn't valid.
} _net_mqtt_ack_result_t;

// Peeks for, and if present reads+validates, a fixed 4-byte "simple ack"
// packet - the shape PUBACK, PUBREC, and PUBCOMP all share: fixed
// header, Remaining Length always 2, a 2-byte packet id, no payload. So
// this reads it as one known-size block rather than needing a general
// variable-length packet parser (that's for a future PUBLISH/SUBACK
// reader). Caller must already hold the lock; this doesn't take it.
static _net_mqtt_ack_result_t
_net_mqtt_poll_read_ack(sys_iostream_t *conn, uint32_t timeout_ms,
                        uint8_t expected_type, uint16_t expected_packet_id) {
  if (sys_iostream_peek(conn) < 0) {
    return _net_mqtt_ack_none;
  }
  uint8_t buf[4];
  if (!_net_mqtt_read_exact(conn, buf, sizeof(buf), timeout_ms)) {
    return _net_mqtt_ack_bad;
  }
  uint16_t got_packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
  if (buf[0] != expected_type || buf[1] != 0x02 ||
      got_packet_id != expected_packet_id) {
    return _net_mqtt_ack_bad;
  }
  return _net_mqtt_ack_ok;
}

// Checks for (and, if present, reads) a PUBACK for the QoS 1 publish
// currently awaited - completes it (fires net_mqtt_event_sent) on a
// match. Anything else arriving here (an unexpected packet type, or a
// mismatched packet id) is treated as a protocol error for now, since
// there's no general dispatch yet to hand it to instead.
static bool _net_mqtt_poll_publish_read_puback(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  if (_net_mqtt_singleton.publish.state != _net_mqtt_publish_qos1_wait_puback) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  sys_iostream_t *conn = _net_mqtt_singleton.conn;
  uint32_t timeout_ms = _net_mqtt_singleton.timeout_ms;
  uint32_t message_id = _net_mqtt_singleton.publish.message_id;
  uint16_t packet_id = _net_mqtt_singleton.publish.packet_id;

  _net_mqtt_ack_result_t result = _net_mqtt_poll_read_ack(
      conn, timeout_ms, _NET_MQTT_PACKET_PUBACK, packet_id);
  if (result == _net_mqtt_ack_none) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }
  if (result == _net_mqtt_ack_bad) {
    _net_mqtt_abort_connection_locked(); // also resets publish.state
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    _net_mqtt_publish_fail(message_id, "malformed or unexpected PUBACK");
    return true;
  }

  const char *topic = _net_mqtt_singleton.publish.topic;
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(_net_mqtt_singleton.publish_cond);
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = message_id}};
  _net_mqtt_fire_event(&sent_event);
  return true;
}

// Checks for (and, if present, reads) a PUBREC for the QoS 2 publish
// currently awaited. On a match, immediately sends PUBREL - a small,
// fixed-shape packet, much like DISCONNECT - and moves to
// qos2_wait_pubcomp: *not* done yet, see that state's own doc on why.
static bool _net_mqtt_poll_publish_read_pubrec(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  if (_net_mqtt_singleton.publish.state != _net_mqtt_publish_qos2_wait_pubrec) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  sys_iostream_t *conn = _net_mqtt_singleton.conn;
  uint32_t timeout_ms = _net_mqtt_singleton.timeout_ms;
  uint32_t message_id = _net_mqtt_singleton.publish.message_id;
  uint16_t packet_id = _net_mqtt_singleton.publish.packet_id;

  _net_mqtt_ack_result_t result = _net_mqtt_poll_read_ack(
      conn, timeout_ms, _NET_MQTT_PACKET_PUBREC, packet_id);
  if (result == _net_mqtt_ack_none) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }
  if (result == _net_mqtt_ack_bad) {
    _net_mqtt_abort_connection_locked(); // also resets publish.state
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    _net_mqtt_publish_fail(message_id, "malformed or unexpected PUBREC");
    return true;
  }

  // PUBREL: fixed header (type + spec-mandated reserved flags, already
  // complete in _NET_MQTT_PACKET_PUBREL - see its own doc), Remaining
  // Length always 2, then the same packet id PUBREC just confirmed.
  uint8_t pubrel[4] = {_NET_MQTT_PACKET_PUBREL, 0x02,
                       (uint8_t)(packet_id >> 8), (uint8_t)(packet_id & 0xFF)};
  if (!_net_mqtt_write_exact(conn, pubrel, sizeof(pubrel), timeout_ms)) {
    _net_mqtt_abort_connection_locked();
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    _net_mqtt_publish_fail(message_id, "PUBREL write failed");
    return true;
  }

  _net_mqtt_singleton.publish.state = _net_mqtt_publish_qos2_wait_pubcomp;
  _net_mqtt_singleton.publish.sent_at_ms = sys_timestamp_ms();
  sys_mutex_unlock(_net_mqtt_singleton.lock);
  return true;
}

// Checks for (and, if present, reads) a PUBCOMP for the QoS 2 publish
// currently awaited - the final leg of the chain; a match here is what
// actually completes a QoS 2 publish (fires net_mqtt_event_sent).
static bool _net_mqtt_poll_publish_read_pubcomp(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  if (_net_mqtt_singleton.publish.state != _net_mqtt_publish_qos2_wait_pubcomp) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  sys_iostream_t *conn = _net_mqtt_singleton.conn;
  uint32_t timeout_ms = _net_mqtt_singleton.timeout_ms;
  uint32_t message_id = _net_mqtt_singleton.publish.message_id;
  uint16_t packet_id = _net_mqtt_singleton.publish.packet_id;

  _net_mqtt_ack_result_t result = _net_mqtt_poll_read_ack(
      conn, timeout_ms, _NET_MQTT_PACKET_PUBCOMP, packet_id);
  if (result == _net_mqtt_ack_none) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }
  if (result == _net_mqtt_ack_bad) {
    _net_mqtt_abort_connection_locked(); // also resets publish.state
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    _net_mqtt_publish_fail(message_id, "malformed or unexpected PUBCOMP");
    return true;
  }

  const char *topic = _net_mqtt_singleton.publish.topic;
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(_net_mqtt_singleton.publish_cond);
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = message_id}};
  _net_mqtt_fire_event(&sent_event);
  return true;
}

// Fails (rather than hangs forever on) a QoS 1/2 publish whose next
// expected reply never arrived within timeout_ms of the last packet this
// module sent for it (the original PUBLISH, or - for QoS 2's second leg
// - the PUBREL, whichever is most recent: see sent_at_ms's own doc). No
// retry (the spec's own answer - resend with DUP=1, or for QoS 2 past
// PUBREC, just resend PUBREL) yet; just frees the slot and reports the
// failure. Doesn't tear down the connection - one slow/lost reply isn't
// necessarily a dead connection, so future publishes still get to try
// their own luck.
static bool _net_mqtt_poll_publish_check_timeout(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  _net_mqtt_publish_state_t state = _net_mqtt_singleton.publish.state;
  bool awaiting_reply = state == _net_mqtt_publish_qos1_wait_puback ||
                       state == _net_mqtt_publish_qos2_wait_pubrec ||
                       state == _net_mqtt_publish_qos2_wait_pubcomp;

  if (!awaiting_reply || sys_timestamp_ms() - _net_mqtt_singleton.publish.sent_at_ms <
                            _net_mqtt_singleton.timeout_ms) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  uint32_t message_id = _net_mqtt_singleton.publish.message_id;
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(_net_mqtt_singleton.publish_cond);
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t error_event = {
      .type = net_mqtt_event_error,
      .data.error = {.message = "acknowledgment timed out", .message_id = message_id}};
  _net_mqtt_fire_event(&error_event);
  return true;
}

bool _net_mqtt_poll(void) {
  if (!_net_mqtt_singleton.active || !_net_mqtt_singleton.connected) {
    return false;
  }
  bool sent = _net_mqtt_poll_publish_send();
  bool acked = _net_mqtt_poll_publish_read_puback();
  bool rec = _net_mqtt_poll_publish_read_pubrec();
  bool comp = _net_mqtt_poll_publish_read_pubcomp();
  bool timed_out = _net_mqtt_poll_publish_check_timeout();
  return sent || acked || rec || comp || timed_out;
}

#include "private.h"
#include <string.h>

// The small per-publish state machine this module drives - see
// _net_mqtt_publish_state_t's own doc for the states themselves. A
// general incoming-packet reader for anything beyond PUBACK (incoming
// PUBLISH dispatch for net_mqtt_subscribe(), PINGRESP for the
// keep-alive) still needs adding here too, alongside those features -
// _net_mqtt_poll_publish_read_puback() is the narrow, PUBACK-only
// version of what that will eventually become.

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

// Sends whatever net_mqtt_publish() has staged (QoS 0 or QoS 1) - see
// net_mqtt_publish()'s own doc on why sending happens here rather than
// synchronously in that call.
static bool _net_mqtt_poll_publish_send(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  _net_mqtt_publish_state_t state = _net_mqtt_singleton.publish.state;
  if (state != _net_mqtt_publish_qos0 && state != _net_mqtt_publish_qos1) {
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

  // Fixed header: packet type plus DUP/QoS/RETAIN flags in the low
  // nibble - DUP always 0 (no retransmission tracking), QoS bits 01 for
  // QoS 1 (00 for QoS 0), RETAIN from the staged parameter. Variable
  // header: Topic Name, THEN - QoS 1 only - a 2-byte Packet Identifier
  // (that order matters - it comes after the topic, not before it).
  // Payload: the raw message bytes, un-prefixed (length implied by
  // Remaining Length minus everything before it).
  size_t topic_len = strlen(topic);
  uint32_t remaining_length =
      2 + (uint32_t)topic_len + (is_qos1 ? 2u : 0u) + (uint32_t)payload_len;

  uint8_t header[1 + 4 + 2];
  size_t pos = 0;
  uint8_t qos_flags = is_qos1 ? _NET_MQTT_PUBLISH_FLAG_QOS1 : 0;
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
        (!is_qos1 ||
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

  if (is_qos1) {
    // Not done yet - stays occupied until a matching PUBACK arrives (or
    // times out) - see _net_mqtt_publish_qos1_wait_puback's own doc on
    // why "sent" isn't the same as "acknowledged" for QoS 1. No event
    // fires yet, and the slot isn't freed (no broadcast) either.
    _net_mqtt_singleton.publish.state = _net_mqtt_publish_qos1_wait_puback;
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

// Checks for (and, if present, reads) a PUBACK for the QoS 1 publish
// currently awaited - a no-op if nothing's staged in that state, or
// nothing's arrived yet. PUBACK is fixed-shape like CONNACK - fixed
// header, Remaining Length always 2, a 2-byte packet id, no payload - so
// this reads it as one known-size block rather than needing a general
// variable-length packet parser (that's for a future PUBLISH/SUBACK
// reader). Anything else arriving here (an unexpected packet type, or a
// mismatched packet id) is treated as a protocol error for now, since
// there's no general dispatch yet to hand it to instead.
static bool _net_mqtt_poll_publish_read_puback(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  if (_net_mqtt_singleton.publish.state != _net_mqtt_publish_qos1_wait_puback) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  sys_iostream_t *conn = _net_mqtt_singleton.conn;
  int peeked = sys_iostream_peek(conn);
  if (peeked < 0) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false; // Nothing available this round - try again next poll().
  }

  uint32_t timeout_ms = _net_mqtt_singleton.timeout_ms;
  uint32_t expected_message_id = _net_mqtt_singleton.publish.message_id;
  uint16_t expected_packet_id = _net_mqtt_singleton.publish.packet_id;

  uint8_t puback[4];
  bool ok = _net_mqtt_read_exact(conn, puback, sizeof(puback), timeout_ms);
  if (ok) {
    uint16_t got_packet_id = (uint16_t)((puback[2] << 8) | puback[3]);
    ok = puback[0] == _NET_MQTT_PACKET_PUBACK && puback[1] == 0x02 &&
        got_packet_id == expected_packet_id;
  }

  if (!ok) {
    _net_mqtt_abort_connection_locked(); // also resets publish.state
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    _net_mqtt_publish_fail(expected_message_id, "malformed or unexpected PUBACK");
    return true;
  }

  const char *topic = _net_mqtt_singleton.publish.topic;
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;
  sys_cond_broadcast(_net_mqtt_singleton.publish_cond);
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t sent_event = {
      .type = net_mqtt_event_sent,
      .data.sent = {.topic = topic, .message_id = expected_message_id}};
  _net_mqtt_fire_event(&sent_event);
  return true;
}

// Fails (rather than hangs forever on) a QoS 1 publish whose PUBACK never
// arrived within timeout_ms of the PUBLISH going out. No retry (the
// spec's own answer - resend with DUP=1) yet; just frees the slot and
// reports the failure. Doesn't tear down the connection - one slow/lost
// ack isn't necessarily a dead connection, so future publishes still get
// to try their own luck.
static bool _net_mqtt_poll_publish_check_timeout(void) {
  sys_mutex_lock(_net_mqtt_singleton.lock);

  if (_net_mqtt_singleton.publish.state != _net_mqtt_publish_qos1_wait_puback ||
      sys_timestamp_ms() - _net_mqtt_singleton.publish.sent_at_ms <
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
      .data.error = {.message = "PUBACK timed out", .message_id = message_id}};
  _net_mqtt_fire_event(&error_event);
  return true;
}

bool _net_mqtt_poll(void) {
  if (!_net_mqtt_singleton.active || !_net_mqtt_singleton.connected) {
    return false;
  }
  bool sent = _net_mqtt_poll_publish_send();
  bool acked = _net_mqtt_poll_publish_read_puback();
  bool timed_out = _net_mqtt_poll_publish_check_timeout();
  return sent || acked || timed_out;
}

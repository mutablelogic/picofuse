#include "private.h"
#include <string.h>

// The small per-publish/subscribe/unsubscribe state machines this module
// drives - see _net_mqtt_publish_state_t/_net_mqtt_subscribe_state_t/
// _net_mqtt_unsubscribe_state_t's own doc for the states themselves.
// Incoming replies are read via _net_mqtt_poll_read_dispatch() below,
// which peeks the fixed header's type nibble first and routes to the
// matching per-type handler - each handler that's a reply to something
// this client itself sent then sys_assert()s that its own state machine
// agrees it should be expecting exactly that type, rather than
// independently peeking and guessing "is this mine?" the way this file
// used to. That guess-per-handler pattern was only safe while PUBACK/
// PUBREC/PUBCOMP shared one state machine with nothing else ever
// arriving unprompted; SUBACK/UNSUBACK (more than one kind of reply
// legitimately in flight at once) and unprompted incoming PUBLISH
// delivery (not a reply to anything at all, so nothing to assert against
// - see _net_mqtt_poll_read_publish()'s own doc) both break that
// assumption - the dispatcher is what makes routing them all safe.
//
// _net_mqtt_poll() (bottom of this file) is the only place in this
// module that touches _net_mqtt_singleton directly - it's the root entry
// from net_poll() (no caller-supplied handle exists there to use
// instead, same reasoning as net_mqtt_init()'s own doc), and every
// helper below it takes the resulting net_mqtt_t* as a parameter rather
// than reaching for the singleton itself.

// Fires net_mqtt_event_error/net_mqtt_event_disconnected for a broken
// connection - the common tail of a mid-packet write failure or a
// malformed/unexpected reply, whichever of publish/subscribe/unsubscribe
// it happened to. Caller must NOT be holding the lock (both events fire
// from here, and this module never fires one while locked).
static void _net_mqtt_poll_fail(net_mqtt_t *mqtt, uint32_t message_id,
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
    _net_mqtt_poll_fail(mqtt, message_id, "PUBLISH write failed");
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

// Sends whatever net_mqtt_subscribe() has staged - see
// net_mqtt_subscribe()'s own doc on why sending happens here rather than
// synchronously in that call. Only ever one topic filter per SUBSCRIBE -
// this client never batches several net_mqtt_subscribe() calls into one
// packet, so the payload is always exactly one (Topic Filter, Requested
// QoS) pair.
static bool _net_mqtt_poll_subscribe_send(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  if (mqtt->subscribe.state != _net_mqtt_subscribe_requesting) {
    sys_mutex_unlock(mqtt->lock);
    return false; // Nothing staged to send right now.
  }

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->subscribe.message_id;
  uint16_t packet_id = mqtt->subscribe.packet_id;
  net_mqtt_qos_t requested_qos = mqtt->subscribe.requested_qos;
  size_t filter_len = strlen(mqtt->subscribe.filter);

  // Fixed header. Variable header: 2-byte Packet Identifier. Payload:
  // length-prefixed Topic Filter, then a single Requested QoS byte.
  uint32_t remaining_length = 2 + 2 + (uint32_t)filter_len + 1;

  uint8_t header[1 + 4 + 2 + 2];
  size_t pos = 0;
  header[pos++] = _NET_MQTT_PACKET_SUBSCRIBE;

  size_t len_n = _net_mqtt_encode_length(remaining_length, header + pos,
                                         sizeof(header) - pos);
  bool ok = len_n != 0;
  if (ok) {
    pos += len_n;
    header[pos++] = (uint8_t)(packet_id >> 8);
    header[pos++] = (uint8_t)(packet_id & 0xFF);
    header[pos++] = (uint8_t)(filter_len >> 8);
    header[pos++] = (uint8_t)(filter_len & 0xFF);

    uint8_t qos_byte = (uint8_t)requested_qos;

    ok = _net_mqtt_write_exact(conn, header, pos, timeout_ms) &&
         _net_mqtt_write_exact(conn, mqtt->subscribe.filter, filter_len,
                               timeout_ms) &&
         _net_mqtt_write_exact(conn, &qos_byte, sizeof(qos_byte), timeout_ms);
  }

  if (!ok) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets subscribe.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, message_id, "SUBSCRIBE write failed");
    return true;
  }

  mqtt->subscribe.state = _net_mqtt_subscribe_wait_suback;
  mqtt->subscribe.sent_at_ms = sys_timestamp_ms();
  sys_mutex_unlock(mqtt->lock);
  return true;
}

// Sends whatever net_mqtt_unsubscribe() has staged - same reasoning as
// _net_mqtt_poll_subscribe_send(), and likewise always exactly one Topic
// Filter in the payload (no Requested QoS byte this time - UNSUBSCRIBE
// carries none).
static bool _net_mqtt_poll_unsubscribe_send(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  if (mqtt->unsubscribe.state != _net_mqtt_unsubscribe_requesting) {
    sys_mutex_unlock(mqtt->lock);
    return false; // Nothing staged to send right now.
  }

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->unsubscribe.message_id;
  uint16_t packet_id = mqtt->unsubscribe.packet_id;
  size_t filter_len = strlen(mqtt->unsubscribe.filter);

  uint32_t remaining_length = 2 + 2 + (uint32_t)filter_len;

  uint8_t header[1 + 4 + 2 + 2];
  size_t pos = 0;
  header[pos++] = _NET_MQTT_PACKET_UNSUBSCRIBE;

  size_t len_n = _net_mqtt_encode_length(remaining_length, header + pos,
                                         sizeof(header) - pos);
  bool ok = len_n != 0;
  if (ok) {
    pos += len_n;
    header[pos++] = (uint8_t)(packet_id >> 8);
    header[pos++] = (uint8_t)(packet_id & 0xFF);
    header[pos++] = (uint8_t)(filter_len >> 8);
    header[pos++] = (uint8_t)(filter_len & 0xFF);

    ok = _net_mqtt_write_exact(conn, header, pos, timeout_ms) &&
         _net_mqtt_write_exact(conn, mqtt->unsubscribe.filter, filter_len,
                               timeout_ms);
  }

  if (!ok) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets unsubscribe.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, message_id, "UNSUBSCRIBE write failed");
    return true;
  }

  mqtt->unsubscribe.state = _net_mqtt_unsubscribe_wait_unsuback;
  mqtt->unsubscribe.sent_at_ms = sys_timestamp_ms();
  sys_mutex_unlock(mqtt->lock);
  return true;
}

// Reads+validates a fixed 4-byte "simple ack" packet - the shape PUBACK,
// PUBREC, PUBCOMP, and UNSUBACK all share: fixed header, Remaining
// Length always 2, a 2-byte packet id, no payload (SUBACK is the odd one
// out - it also carries return codes, so it needs its own reader below).
// Caller must already hold the lock and must already have confirmed (via
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
    _net_mqtt_poll_fail(mqtt, message_id, "malformed or unexpected PUBACK");
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
    _net_mqtt_poll_fail(mqtt, message_id, "malformed or unexpected PUBREC");
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
    _net_mqtt_poll_fail(mqtt, message_id, "PUBREL write failed");
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
    _net_mqtt_poll_fail(mqtt, message_id, "malformed or unexpected PUBCOMP");
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

// Reads the SUBACK for the subscribe currently awaited - completes it
// (fires net_mqtt_event_subscribed) on a match, or fires
// net_mqtt_event_error and frees the reserved topics[] slot back (see
// _net_mqtt_topic_alloc()'s own doc) if the broker denied it (return
// code 0x80). Caller (the dispatcher) already holds the lock and has
// already peeked a SUBACK type byte.
//
// Unlike PUBACK/PUBREC/PUBCOMP/UNSUBACK, SUBACK isn't a fixed 4-byte
// shape - it carries one return code byte per Topic Filter in the
// original SUBSCRIBE. This client only ever sends one filter per
// SUBSCRIBE (see _net_mqtt_poll_subscribe_send()'s own doc), so Remaining
// Length is always exactly 3 (2-byte packet id + 1 return code) and the
// whole packet is always exactly 5 bytes - read directly rather than via
// the shared _net_mqtt_poll_read_ack() helper, which assumes Remaining
// Length 2.
static bool _net_mqtt_poll_subscribe_read_suback(net_mqtt_t *mqtt) {
  sys_assert(mqtt->subscribe.state == _net_mqtt_subscribe_wait_suback);

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->subscribe.message_id;
  uint16_t packet_id = mqtt->subscribe.packet_id;
  _net_mqtt_topic_t *topic = mqtt->subscribe.topic;

  uint8_t buf[5];
  bool ok = _net_mqtt_read_exact(conn, buf, sizeof(buf), timeout_ms);
  uint16_t got_packet_id = ok ? (uint16_t)((buf[2] << 8) | buf[3]) : 0;
  if (!ok || buf[0] != _NET_MQTT_PACKET_SUBACK || buf[1] != 0x03 ||
      got_packet_id != packet_id) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets subscribe.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, message_id, "malformed or unexpected SUBACK");
    return true;
  }

  uint8_t return_code = buf[4];
  if (return_code == 0x80) {
    // Denied - the subscription never actually happened, so the slot
    // reserved for it at stage time never gets used.
    _net_mqtt_topic_free(mqtt, topic);
    mqtt->subscribe.topic = NULL;
    mqtt->subscribe.state = _net_mqtt_subscribe_idle;
    sys_cond_broadcast(mqtt->publish_cond);
    sys_mutex_unlock(mqtt->lock);

    net_mqtt_event_t error_event = {
        .type = net_mqtt_event_error,
        .data.error = {.message = "broker refused SUBSCRIBE",
                       .message_id = message_id}};
    _net_mqtt_fire_event(mqtt, &error_event);
    return true;
  }

  // Success - fill in the slot _net_mqtt_topic_alloc() reserved back at
  // stage time (see _net_mqtt_subscribe_pending_t::topic's own doc).
  net_mqtt_qos_t granted_qos = (net_mqtt_qos_t)return_code;
  sys_sprintf(topic->filter, sizeof(topic->filter), "%s", mqtt->subscribe.filter);
  topic->granted_qos = granted_qos;

  mqtt->subscribe.topic = NULL;
  mqtt->subscribe.state = _net_mqtt_subscribe_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t subscribed_event = {
      .type = net_mqtt_event_subscribed,
      .data.subscribed = {.granted_qos = granted_qos, .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &subscribed_event);
  return true;
}

// Reads the UNSUBACK for the unsubscribe currently awaited - completes
// it (fires net_mqtt_event_unsubscribed) on a match, freeing the target
// topics[] slot only now (see _net_mqtt_unsubscribe_pending_t::topic's
// own doc on why not at stage time). Caller (the dispatcher) already
// holds the lock and has already peeked an UNSUBACK type byte. UNSUBACK
// carries no return codes (unlike SUBACK) - just the packet id - so it
// fits the same fixed 4-byte shape as PUBACK/PUBREC/PUBCOMP.
static bool _net_mqtt_poll_unsubscribe_read_unsuback(net_mqtt_t *mqtt) {
  sys_assert(mqtt->unsubscribe.state == _net_mqtt_unsubscribe_wait_unsuback);

  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint32_t message_id = mqtt->unsubscribe.message_id;
  uint16_t packet_id = mqtt->unsubscribe.packet_id;
  _net_mqtt_topic_t *topic = mqtt->unsubscribe.topic;

  if (!_net_mqtt_poll_read_ack(conn, timeout_ms, _NET_MQTT_PACKET_UNSUBACK,
                               packet_id)) {
    _net_mqtt_abort_connection_locked(mqtt); // also resets unsubscribe.state
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, message_id, "malformed or unexpected UNSUBACK");
    return true;
  }

  topic->active = false; // Confirmed gone.
  mqtt->unsubscribe.topic = NULL;
  mqtt->unsubscribe.state = _net_mqtt_unsubscribe_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t unsubscribed_event = {
      .type = net_mqtt_event_unsubscribed,
      .data.unsubscribed = {.message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &unsubscribed_event);
  return true;
}

// Decodes an incoming MQTT "Remaining Length" varint - the mirror of
// _net_mqtt_encode_length(), but reading byte-by-byte since (unlike
// encoding) the number of bytes isn't known upfront. Purely a wire-level
// helper - no net_mqtt_t needed.
// @return false on a short read/timeout, or more than 4 bytes (the
// protocol's own ceiling - see _net_mqtt_encode_length()'s own doc).
static bool _net_mqtt_poll_read_length(sys_iostream_t *conn,
                                       uint32_t timeout_ms, uint32_t *out) {
  uint32_t value = 0;
  uint32_t multiplier = 1;
  for (int i = 0; i < 4; i++) {
    uint8_t b;
    if (!_net_mqtt_read_exact(conn, &b, sizeof(b), timeout_ms)) {
      return false;
    }
    value += (uint32_t)(b & 0x7F) * multiplier;
    if ((b & 0x80) == 0) {
      *out = value;
      return true;
    }
    multiplier *= 128;
  }
  return false;
}

// Reads an incoming PUBLISH - a message delivered because of one of this
// handle's net_mqtt_subscribe()'d topic filters - and delivers it as a
// net_mqtt_event_received event. Caller (the dispatcher) already holds
// the lock and has already peeked a PUBLISH type byte; unlike every
// other handler in this file, there's no state machine invariant to
// sys_assert() here - an incoming PUBLISH is legitimately unprompted,
// not a reply to anything this client tracks the state of.
//
// The whole message (topic and payload both) is read off the wire and
// fully buffered before this unlocks and fires the event - net_mqtt_
// received_t's own doc explains why: handing the callback a stream would
// mean letting it keep reading directly from the shared connection after
// this function has released `lock`, and a concurrent net_poll() call on
// another thread/core reading the same connection at the same time would
// corrupt whichever one loses the race. The payload buffer comes from
// the stack for anything _NET_MQTT_PAYLOAD_STACK_SIZE or smaller (the
// common case), or a temporary sys_malloc() - freed right after the
// event fires - for anything larger, up to _NET_MQTT_PAYLOAD_MAX_SIZE.
//
// Only QoS 0 delivery is handled so far - this client only ever grants
// QoS 0 on net_mqtt_subscribe() (see its own doc), and a broker
// downgrades delivery to match the subscriber's own granted QoS, so a
// non-zero QoS here shouldn't happen; if it somehow does anyway, that's
// treated the same as any other packet this client can't process yet.
static bool _net_mqtt_poll_read_publish(net_mqtt_t *mqtt) {
  sys_iostream_t *conn = mqtt->conn;
  uint32_t timeout_ms = mqtt->timeout_ms;

  uint8_t first_byte;
  uint32_t remaining_length;
  if (!_net_mqtt_read_exact(conn, &first_byte, sizeof(first_byte), timeout_ms) ||
      !_net_mqtt_poll_read_length(conn, timeout_ms, &remaining_length)) {
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, 0, "malformed PUBLISH");
    return true;
  }

  bool retain = (first_byte & 0x01) != 0;
  uint8_t qos_bits = (first_byte >> 1) & 0x03;

  uint8_t topic_len_bytes[2];
  if (qos_bits != 0 || remaining_length < 2 ||
      !_net_mqtt_read_exact(conn, topic_len_bytes, sizeof(topic_len_bytes),
                            timeout_ms)) {
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, 0,
                        qos_bits != 0 ? "unsupported PUBLISH QoS"
                                      : "malformed PUBLISH");
    return true;
  }

  size_t topic_len =
      ((size_t)topic_len_bytes[0] << 8) | (size_t)topic_len_bytes[1];
  uint32_t after_topic_len = 2 + (uint32_t)topic_len;
  char topic_buf[NET_MQTT_TOPIC_FILTER_SIZE];
  if (topic_len >= sizeof(topic_buf) || after_topic_len > remaining_length ||
      !_net_mqtt_read_exact(conn, topic_buf, topic_len, timeout_ms)) {
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, 0, "malformed or oversized PUBLISH topic");
    return true;
  }
  topic_buf[topic_len] = '\0';

  size_t payload_len = remaining_length - after_topic_len;

  char payload_stack_buf[_NET_MQTT_PAYLOAD_STACK_SIZE];
  void *payload_heap_buf = NULL;
  void *payload_buf = NULL;
  bool ok = true;

  if (payload_len > 0) {
    if (payload_len > _NET_MQTT_PAYLOAD_MAX_SIZE) {
      ok = false;
    } else if (payload_len <= sizeof(payload_stack_buf)) {
      payload_buf = payload_stack_buf;
    } else {
      payload_heap_buf = sys_malloc(payload_len);
      payload_buf = payload_heap_buf;
      ok = payload_buf != NULL;
    }
    ok = ok && _net_mqtt_read_exact(conn, payload_buf, payload_len, timeout_ms);
  }

  if (!ok) {
    sys_free(payload_heap_buf);
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, 0, "malformed or oversized PUBLISH payload");
    return true;
  }

  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t received_event = {
      .type = net_mqtt_event_received,
      .data.received = {.topic = topic_buf,
                        .payload = payload_buf,
                        .payload_len = payload_len,
                        .retain = retain}};
  _net_mqtt_fire_event(mqtt, &received_event);
  sys_free(payload_heap_buf);
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
  case _NET_MQTT_PACKET_SUBACK:
    return _net_mqtt_poll_subscribe_read_suback(mqtt); // unlocks itself
  case _NET_MQTT_PACKET_UNSUBACK:
    return _net_mqtt_poll_unsubscribe_read_unsuback(mqtt); // unlocks itself
  case _NET_MQTT_PACKET_PUBLISH:
    return _net_mqtt_poll_read_publish(mqtt); // unlocks itself
  default:
    // PINGRESP reading isn't implemented yet (see this file's own
    // top-of-file note). Either way there's nowhere for this packet to
    // go - and it can't just be left on the wire for a future poll() to
    // reconsider, since that would desync framing for whatever's read
    // next - so for now it's treated the same as a malformed reply from
    // an operation that was actually pending.
    _net_mqtt_abort_connection_locked(mqtt);
    sys_mutex_unlock(mqtt->lock);
    _net_mqtt_poll_fail(mqtt, 0, "unexpected or not-yet-supported packet type");
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

// Fails a subscribe whose SUBACK never arrived within timeout_ms of the
// SUBSCRIBE this module sent for it - same reasoning as
// _net_mqtt_poll_publish_check_timeout(), including the same note on a
// late SUBACK then tripping the dispatcher's sys_assert(). Frees the
// topics[] slot reserved at stage time, same as an explicit denial.
static bool _net_mqtt_poll_subscribe_check_timeout(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  if (mqtt->subscribe.state != _net_mqtt_subscribe_wait_suback ||
      sys_timestamp_ms() - mqtt->subscribe.sent_at_ms < mqtt->timeout_ms) {
    sys_mutex_unlock(mqtt->lock);
    return false;
  }

  uint32_t message_id = mqtt->subscribe.message_id;
  _net_mqtt_topic_free(mqtt, mqtt->subscribe.topic);
  mqtt->subscribe.topic = NULL;
  mqtt->subscribe.state = _net_mqtt_subscribe_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t error_event = {
      .type = net_mqtt_event_error,
      .data.error = {.message = "SUBACK timed out", .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &error_event);
  return true;
}

// Fails an unsubscribe whose UNSUBACK never arrived within timeout_ms -
// same reasoning as _net_mqtt_poll_subscribe_check_timeout(). The target
// topics[] slot is left exactly as it was (still active/subscribed) -
// unlike a subscribe's reserved slot, there's nothing tentative about it
// to free; as far as this client can tell, it's still subscribed.
static bool _net_mqtt_poll_unsubscribe_check_timeout(net_mqtt_t *mqtt) {
  sys_mutex_lock(mqtt->lock);

  if (mqtt->unsubscribe.state != _net_mqtt_unsubscribe_wait_unsuback ||
      sys_timestamp_ms() - mqtt->unsubscribe.sent_at_ms < mqtt->timeout_ms) {
    sys_mutex_unlock(mqtt->lock);
    return false;
  }

  uint32_t message_id = mqtt->unsubscribe.message_id;
  mqtt->unsubscribe.topic = NULL;
  mqtt->unsubscribe.state = _net_mqtt_unsubscribe_idle;
  sys_cond_broadcast(mqtt->publish_cond);
  sys_mutex_unlock(mqtt->lock);

  net_mqtt_event_t error_event = {
      .type = net_mqtt_event_error,
      .data.error = {.message = "UNSUBACK timed out", .message_id = message_id}};
  _net_mqtt_fire_event(mqtt, &error_event);
  return true;
}

bool _net_mqtt_poll(void) {
  net_mqtt_t *mqtt = &_net_mqtt_singleton;
  if (!mqtt->active || !mqtt->connected) {
    return false;
  }
  // Stops at the first of these that actually does something, rather
  // than always running all of them - each can involve blocking I/O, so
  // servicing more than one per call would tie up the caller for their
  // combined latency instead of spreading the work across separate
  // net_poll() calls.
  if (_net_mqtt_poll_publish_send(mqtt)) {
    return true;
  }
  if (_net_mqtt_poll_subscribe_send(mqtt)) {
    return true;
  }
  if (_net_mqtt_poll_unsubscribe_send(mqtt)) {
    return true;
  }
  if (_net_mqtt_poll_read_dispatch(mqtt)) {
    return true;
  }
  if (_net_mqtt_poll_publish_check_timeout(mqtt)) {
    return true;
  }
  if (_net_mqtt_poll_subscribe_check_timeout(mqtt)) {
    return true;
  }
  return _net_mqtt_poll_unsubscribe_check_timeout(mqtt);
}

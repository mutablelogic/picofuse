#include "private.h"
#include <string.h>

// _net_mqtt_next_message_id()/_net_mqtt_next_packet_id() are shared with
// subscribe.c - both operations draw packet ids from the same namespace
// per the spec - so they live in io.c now; see private.h's declarations.

// Stages a QoS 0 publish - fire and forget, no packet id, no ack to wait
// for. poll.c sends it and immediately fires net_mqtt_event_sent.
static uint32_t _net_mqtt_publish_stage_qos0(net_mqtt_t *mqtt,
                                             const char *topic,
                                             const void *payload,
                                             size_t payload_len, bool retain) {
  uint32_t message_id = _net_mqtt_next_message_id(mqtt);
  mqtt->publish.state = _net_mqtt_publish_qos0;
  mqtt->publish.topic = topic;
  mqtt->publish.payload = payload;
  mqtt->publish.payload_len = payload_len;
  mqtt->publish.retain = retain;
  mqtt->publish.message_id = message_id;
  return message_id;
}

// Stages a QoS 1 publish - needs a packet id (unlike QoS 0), and poll.c
// keeps the slot occupied through the PUBACK wait rather than freeing it
// right after the write - see _net_mqtt_publish_qos1_wait_puback's own
// doc on why.
static uint32_t _net_mqtt_publish_stage_qos1(net_mqtt_t *mqtt,
                                             const char *topic,
                                             const void *payload,
                                             size_t payload_len, bool retain) {
  uint32_t message_id = _net_mqtt_next_message_id(mqtt);
  mqtt->publish.state = _net_mqtt_publish_qos1;
  mqtt->publish.topic = topic;
  mqtt->publish.payload = payload;
  mqtt->publish.payload_len = payload_len;
  mqtt->publish.retain = retain;
  mqtt->publish.message_id = message_id;
  mqtt->publish.packet_id = _net_mqtt_next_packet_id(mqtt);
  return message_id;
}

// Stages a QoS 2 publish - same shape as QoS 1's own staging (packet id
// included), poll.c just carries it through the longer
// PUBREC/PUBREL/PUBCOMP chain instead of stopping at PUBACK - see
// _net_mqtt_publish_qos2_wait_pubcomp's own doc on why.
static uint32_t _net_mqtt_publish_stage_qos2(net_mqtt_t *mqtt,
                                             const char *topic,
                                             const void *payload,
                                             size_t payload_len, bool retain) {
  uint32_t message_id = _net_mqtt_next_message_id(mqtt);
  mqtt->publish.state = _net_mqtt_publish_qos2;
  mqtt->publish.topic = topic;
  mqtt->publish.payload = payload;
  mqtt->publish.payload_len = payload_len;
  mqtt->publish.retain = retain;
  mqtt->publish.message_id = message_id;
  mqtt->publish.packet_id = _net_mqtt_next_packet_id(mqtt);
  return message_id;
}

uint32_t net_mqtt_publish(net_mqtt_t *mqtt, const char *topic,
                          const void *payload, size_t payload_len,
                          net_mqtt_qos_t qos, bool retain) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic == NULL ||
      (payload_len > 0 && payload == NULL)) {
    return 0;
  }

  // A PUBLISH's Topic Name (unlike a SUBSCRIBE's Topic Filter - see
  // net_mqtt_subscribe()'s own doc) is a concrete destination, not a
  // pattern - the spec requires at least one character and forbids the
  // wildcard characters ('+'/'#') a filter is allowed to use, since a
  // publish has to name exactly one topic, not match a set of them. A
  // compliant broker may reject or even close the connection over a
  // Topic Name that breaks either rule, so it's rejected here instead.
  size_t topic_len = strlen(topic);
  if (topic_len == 0 || strchr(topic, '+') != NULL ||
      strchr(topic, '#') != NULL) {
    return 0;
  }

  // Topic Name Length is a 2-byte field on the wire (see
  // _net_mqtt_poll_publish_send()'s own doc) - a topic this client can't
  // even encode a correct length prefix for is rejected here rather than
  // silently truncating that prefix while still writing the full topic
  // bytes, which would desync the connection's framing for whatever's
  // sent after it.
  if (topic_len > 0xFFFF) {
    return 0;
  }

  // Remaining Length itself is checked in full 64-bit arithmetic, not
  // narrowed to uint32_t first - topic_len/payload_len are size_t, with
  // no ceiling of their own on a 64-bit host, so summing them as
  // uint32_t could wrap around to a small value that looks valid while
  // still writing every real byte, corrupting framing the same way an
  // unchecked topic_len would. 0x0FFFFFFF is the protocol's own ceiling
  // - see _net_mqtt_encode_length()'s own doc.
  bool needs_packet_id = qos == net_mqtt_qos_1 || qos == net_mqtt_qos_2;
  uint64_t remaining_length = 2 + (uint64_t)topic_len +
                              (needs_packet_id ? 2u : 0u) +
                              (uint64_t)payload_len;
  if (remaining_length > 0x0FFFFFFFu) {
    return 0;
  }

  sys_mutex_lock(mqtt->lock);

  // Wait for either a free publish slot or a disconnect - see
  // net_mqtt_publish()'s own doc. sys_cond_timedwait() atomically
  // releases the lock for the wait and reacquires it before returning,
  // so a concurrent net_poll()/net_mqtt_disconnect() on another
  // thread/core can still make progress (and signal publish_cond) while
  // this call is parked here - see private.h's own note on why
  // lock/publish_cond are never destroyed, which this depends on.
  //
  // timeout_ms == 0 means "don't wait at all" throughout this module
  // (see _net_mqtt_write_exact()/_net_mqtt_read_exact()) - sys_cond_
  // timedwait()'s own convention for 0 is the opposite ("wait forever"),
  // so it's never actually called with 0; a single predicate check
  // stands in for it instead, consistent with the rest of the module.
  //
  // publish_cond is shared well beyond just this wait (subscribe/
  // unsubscribe wait on it too, and it's broadcast on every disconnect),
  // so a wake here is routinely spurious as far as this particular call
  // is concerned - re-passing the full timeout_ms to each
  // sys_cond_timedwait() call would let those unrelated wakes keep
  // restarting the clock, so a caller could block well past its own
  // documented timeout_ms under sustained unrelated traffic. Tracking
  // elapsed time against a fixed deadline instead keeps the *total* wait
  // bounded by timeout_ms regardless of how many spurious wakes happen
  // along the way.
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint64_t wait_start_ms = sys_timestamp_ms();
  while (mqtt->connected && mqtt->publish.state != _net_mqtt_publish_idle) {
    uint64_t elapsed_ms = sys_timestamp_ms() - wait_start_ms;
    if (timeout_ms == 0 || elapsed_ms >= timeout_ms ||
        !sys_cond_timedwait(mqtt->publish_cond, mqtt->lock,
                            timeout_ms - (uint32_t)elapsed_ms)) {
      sys_mutex_unlock(mqtt->lock);
      return 0; // Timed out (or timeout_ms == 0) still waiting for a
                // free slot.
    }
  }

  if (!mqtt->connected) {
    sys_mutex_unlock(mqtt->lock);
    return 0;
  }

  uint32_t message_id;
  switch (qos) {
  case net_mqtt_qos_0:
    message_id =
        _net_mqtt_publish_stage_qos0(mqtt, topic, payload, payload_len, retain);
    break;
  case net_mqtt_qos_1:
    message_id =
        _net_mqtt_publish_stage_qos1(mqtt, topic, payload, payload_len, retain);
    break;
  case net_mqtt_qos_2:
    message_id =
        _net_mqtt_publish_stage_qos2(mqtt, topic, payload, payload_len, retain);
    break;
  default:
    sys_mutex_unlock(mqtt->lock);
    return 0;
  }

  sys_mutex_unlock(mqtt->lock);
  return message_id;
}

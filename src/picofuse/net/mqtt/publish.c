#include "private.h"

// Allocates the next client-side message id, skipping 0 on wraparound -
// see net_mqtt_t::next_message_id's own doc on why (0 is our own
// "failure" sentinel, not a protocol requirement the way packet ids are).
static uint32_t _net_mqtt_next_message_id(void) {
  uint32_t id = ++_net_mqtt_singleton.next_message_id;
  if (id == 0) {
    id = ++_net_mqtt_singleton.next_message_id;
  }
  return id;
}

// Allocates the next wire-level Packet Identifier, skipping 0 - see
// net_mqtt_t::next_packet_id's own doc on why (the protocol itself
// reserves 0, not just a "failure" convention of our own).
static uint16_t _net_mqtt_next_packet_id(void) {
  uint16_t id = ++_net_mqtt_singleton.next_packet_id;
  if (id == 0) {
    id = ++_net_mqtt_singleton.next_packet_id;
  }
  return id;
}

// Stages a QoS 0 publish - fire and forget, no packet id, no ack to wait
// for. poll.c sends it and immediately fires net_mqtt_event_sent.
static uint32_t _net_mqtt_publish_stage_qos0(const char *topic,
                                             const void *payload,
                                             size_t payload_len, bool retain) {
  uint32_t message_id = _net_mqtt_next_message_id();
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_qos0;
  _net_mqtt_singleton.publish.topic = topic;
  _net_mqtt_singleton.publish.payload = payload;
  _net_mqtt_singleton.publish.payload_len = payload_len;
  _net_mqtt_singleton.publish.retain = retain;
  _net_mqtt_singleton.publish.message_id = message_id;
  return message_id;
}

// Stages a QoS 1 publish - needs a packet id (unlike QoS 0), and poll.c
// keeps the slot occupied through the PUBACK wait rather than freeing it
// right after the write - see _net_mqtt_publish_qos1_wait_puback's own
// doc on why.
static uint32_t _net_mqtt_publish_stage_qos1(const char *topic,
                                             const void *payload,
                                             size_t payload_len, bool retain) {
  uint32_t message_id = _net_mqtt_next_message_id();
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_qos1;
  _net_mqtt_singleton.publish.topic = topic;
  _net_mqtt_singleton.publish.payload = payload;
  _net_mqtt_singleton.publish.payload_len = payload_len;
  _net_mqtt_singleton.publish.retain = retain;
  _net_mqtt_singleton.publish.message_id = message_id;
  _net_mqtt_singleton.publish.packet_id = _net_mqtt_next_packet_id();
  return message_id;
}

uint32_t net_mqtt_publish(net_mqtt_t *mqtt, const char *topic,
                          const void *payload, size_t payload_len,
                          net_mqtt_qos_t qos, bool retain) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic == NULL ||
      (payload_len > 0 && payload == NULL) ||
      (qos != net_mqtt_qos_0 && qos != net_mqtt_qos_1)) {
    return 0;
  }

  sys_mutex_lock(_net_mqtt_singleton.lock);

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
  uint32_t timeout_ms = _net_mqtt_singleton.timeout_ms;
  while (_net_mqtt_singleton.connected &&
         _net_mqtt_singleton.publish.state != _net_mqtt_publish_idle) {
    if (timeout_ms == 0 ||
        !sys_cond_timedwait(_net_mqtt_singleton.publish_cond,
                            _net_mqtt_singleton.lock, timeout_ms)) {
      sys_mutex_unlock(_net_mqtt_singleton.lock);
      return 0; // Timed out (or timeout_ms == 0) still waiting for a
                // free slot.
    }
  }

  if (!_net_mqtt_singleton.connected) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return 0;
  }

  uint32_t message_id =
      (qos == net_mqtt_qos_0)
          ? _net_mqtt_publish_stage_qos0(topic, payload, payload_len, retain)
          : _net_mqtt_publish_stage_qos1(topic, payload, payload_len, retain);

  sys_mutex_unlock(_net_mqtt_singleton.lock);
  return message_id;
}

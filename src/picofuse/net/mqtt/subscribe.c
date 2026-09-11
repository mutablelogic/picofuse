#include "private.h"

// Reserves a free slot in the confirmed-subscription table, marking it
// active immediately - see net_mqtt_t::topics's own doc. Reserved up
// front at stage time (rather than only once SUBACK actually confirms
// it), so a second net_mqtt_subscribe() call sees accurate room even
// while this one's still in flight, and poll.c's future SUBACK handling
// has nothing left to do but fill in the filter/granted_qos this already
// claimed a slot for (or _net_mqtt_topic_free() it back on denial/
// failure - see _net_mqtt_abort_connection_locked()'s own doc for the
// disconnect case). Caller must already hold the lock.
// @return The reserved slot, or NULL if the pool is full.
static _net_mqtt_topic_t *_net_mqtt_topic_alloc(net_mqtt_t *mqtt) {
  for (size_t i = 0; i < NET_MQTT_TOPIC_CAPACITY; i++) {
    if (!mqtt->topics[i].active) {
      mqtt->topics[i].active = true;
      return &mqtt->topics[i];
    }
  }
  return NULL;
}

void _net_mqtt_topic_free(net_mqtt_t *mqtt, _net_mqtt_topic_t *topic) {
  (void)mqtt;
  if (topic == NULL) {
    return;
  }
  topic->active = false;
}

uint32_t net_mqtt_subscribe(net_mqtt_t *mqtt, const char *topic,
                            net_mqtt_qos_t qos) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic == NULL ||
      qos != net_mqtt_qos_0) {
    return 0; // Only QoS 0 is implemented so far - see net_mqtt_subscribe()'s
              // own doc.
  }

  sys_mutex_lock(mqtt->lock);

  // Wait for either a free subscribe slot or a disconnect - same rules,
  // same shared publish_cond, and the same timeout_ms == 0 convention as
  // net_mqtt_publish() - see its own doc for the full reasoning.
  uint32_t timeout_ms = mqtt->timeout_ms;
  while (mqtt->connected &&
         mqtt->subscribe.state != _net_mqtt_subscribe_idle) {
    if (timeout_ms == 0 ||
        !sys_cond_timedwait(mqtt->publish_cond, mqtt->lock, timeout_ms)) {
      sys_mutex_unlock(mqtt->lock);
      return 0; // Timed out (or timeout_ms == 0) still waiting for a
                // free slot.
    }
  }

  if (!mqtt->connected) {
    sys_mutex_unlock(mqtt->lock);
    return 0;
  }

  _net_mqtt_topic_t *topic_slot = _net_mqtt_topic_alloc(mqtt);
  if (topic_slot == NULL) {
    sys_mutex_unlock(mqtt->lock);
    return 0;
  }

  uint32_t message_id = _net_mqtt_next_message_id(mqtt);
  mqtt->subscribe.state = _net_mqtt_subscribe_requesting;
  sys_sprintf(mqtt->subscribe.filter, sizeof(mqtt->subscribe.filter), "%s",
             topic);
  mqtt->subscribe.requested_qos = qos;
  mqtt->subscribe.message_id = message_id;
  mqtt->subscribe.packet_id = _net_mqtt_next_packet_id(mqtt);
  mqtt->subscribe.topic = topic_slot;

  sys_mutex_unlock(mqtt->lock);
  return message_id;
}

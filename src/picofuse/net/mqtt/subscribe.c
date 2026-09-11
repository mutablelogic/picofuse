#include "private.h"
#include <string.h>

// Finds the confirmed subscription table entry for @p filter, if any -
// used by net_mqtt_unsubscribe() to both validate that @p filter is
// actually subscribed and to locate the slot to eventually free (see
// _net_mqtt_unsubscribe_pending_t::topic's own doc on why that happens
// at confirm time, not here). Deliberately requires `confirmed`, not
// just `active` - see _net_mqtt_topic_t's own doc on why a
// reserved-but-not-yet-confirmed slot must never match here: its
// `filter` isn't meaningful yet (still whatever was last written there,
// possibly by this very filter's own prior, already-removed
// subscription), so matching on it could hand net_mqtt_unsubscribe() a
// slot that's actually reserved for a completely unrelated, still-
// pending net_mqtt_subscribe() call. Caller must already hold the lock.
static _net_mqtt_topic_t *_net_mqtt_topic_find(net_mqtt_t *mqtt,
                                               const char *filter) {
  for (size_t i = 0; i < NET_MQTT_TOPIC_CAPACITY; i++) {
    if (mqtt->topics[i].active && mqtt->topics[i].confirmed &&
        strcmp(mqtt->topics[i].filter, filter) == 0) {
      return &mqtt->topics[i];
    }
  }
  return NULL;
}

// Reserves a free slot in the confirmed-subscription table, marking it
// active (but not yet confirmed - see _net_mqtt_topic_t's own doc)
// immediately - see net_mqtt_t::topics's own doc. Reserved up front at
// stage time (rather than only once SUBACK actually confirms it), so a
// second net_mqtt_subscribe() call sees accurate room even while this
// one's still in flight, and poll.c's future SUBACK handling has nothing
// left to do but fill in the filter/granted_qos this already claimed a
// slot for (or _net_mqtt_topic_free() it back on denial/failure - see
// _net_mqtt_abort_connection_locked()'s own doc for the disconnect
// case). Caller must already hold the lock.
// @return The reserved slot, or NULL if the pool is full.
static _net_mqtt_topic_t *_net_mqtt_topic_alloc(net_mqtt_t *mqtt) {
  for (size_t i = 0; i < NET_MQTT_TOPIC_CAPACITY; i++) {
    if (!mqtt->topics[i].active) {
      mqtt->topics[i].active = true;
      mqtt->topics[i].confirmed = false;
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
  topic->confirmed = false;
}

uint32_t net_mqtt_subscribe(net_mqtt_t *mqtt, const char *topic,
                            net_mqtt_qos_t qos) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic == NULL ||
      qos != net_mqtt_qos_0) {
    return 0; // Only QoS 0 is implemented so far - see net_mqtt_subscribe()'s
              // own doc.
  }
  if (strlen(topic) >= sizeof(mqtt->subscribe.filter)) {
    return 0; // Wouldn't fit NET_MQTT_TOPIC_FILTER_SIZE - see its own
              // doc on why this is rejected rather than left to
              // sys_sprintf()'s own silent truncation below, which would
              // subscribe to a different filter than the one requested.
  }

  sys_mutex_lock(mqtt->lock);

  // Wait for either a free subscribe slot or a disconnect - same rules,
  // same shared publish_cond, same timeout_ms == 0 convention, and the
  // same fixed-deadline (not re-armed on every spurious/unrelated wake)
  // timing as net_mqtt_publish() - see its own doc for the full
  // reasoning on both.
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint64_t wait_start_ms = sys_timestamp_ms();
  while (mqtt->connected &&
         mqtt->subscribe.state != _net_mqtt_subscribe_idle) {
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

uint32_t net_mqtt_unsubscribe(net_mqtt_t *mqtt, const char *topic) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic == NULL) {
    return 0;
  }
  if (strlen(topic) >= sizeof(mqtt->unsubscribe.filter)) {
    return 0; // Same reasoning as net_mqtt_subscribe()'s own check -
              // moot in practice, since nothing this long could ever
              // have been successfully subscribed to in the first
              // place, but that's exactly what makes it safe to reject
              // here too rather than let sys_sprintf() silently
              // truncate it below into matching some other, shorter
              // filter by coincidence.
  }

  sys_mutex_lock(mqtt->lock);

  // Wait for either a free unsubscribe slot or a disconnect - independent
  // of subscribe's own slot (see _net_mqtt_unsubscribe_state_t's own
  // doc), but otherwise the same rules/shared publish_cond/timeout_ms ==
  // 0 convention/fixed-deadline timing as net_mqtt_publish() - see its
  // own doc for the full reasoning on both.
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint64_t wait_start_ms = sys_timestamp_ms();
  while (mqtt->connected &&
         mqtt->unsubscribe.state != _net_mqtt_unsubscribe_idle) {
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

  _net_mqtt_topic_t *topic_slot = _net_mqtt_topic_find(mqtt, topic);
  if (topic_slot == NULL) {
    sys_mutex_unlock(mqtt->lock);
    return 0; // Not currently subscribed to this exact filter.
  }

  uint32_t message_id = _net_mqtt_next_message_id(mqtt);
  mqtt->unsubscribe.state = _net_mqtt_unsubscribe_requesting;
  sys_sprintf(mqtt->unsubscribe.filter, sizeof(mqtt->unsubscribe.filter),
             "%s", topic);
  mqtt->unsubscribe.message_id = message_id;
  mqtt->unsubscribe.packet_id = _net_mqtt_next_packet_id(mqtt);
  mqtt->unsubscribe.topic = topic_slot;

  sys_mutex_unlock(mqtt->lock);
  return message_id;
}

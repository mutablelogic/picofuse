#include "private.h"
#include <string.h>

// Finds the confirmed subscription table entry for @p topic_id, if any -
// used by net_mqtt_unsubscribe() to both validate that @p topic_id is
// actually subscribed and to locate the slot to eventually free (see
// _net_mqtt_unsubscribe_pending_t::topic's own doc on why that happens
// at confirm time, not here). Deliberately requires `confirmed`, not
// just `active` - see _net_mqtt_topic_t's own doc on why a
// reserved-but-not-yet-confirmed slot must never match here: its
// `topic_id` isn't meaningful yet (still whatever was last written
// there, possibly by this very slot's prior, already-removed
// subscription), so matching on it could hand net_mqtt_unsubscribe() a
// slot that's actually reserved for a completely unrelated, still-
// pending net_mqtt_subscribe() call. Caller must already hold the lock.
static _net_mqtt_topic_t *_net_mqtt_topic_find(net_mqtt_t *mqtt,
                                               uint32_t topic_id) {
  for (size_t i = 0; i < NET_MQTT_TOPIC_CAPACITY; i++) {
    if (mqtt->topics[i].active && mqtt->topics[i].confirmed &&
        mqtt->topics[i].topic_id == topic_id) {
      return &mqtt->topics[i];
    }
  }
  return NULL;
}

// True if @p topic_name (a concrete Topic Name off an incoming PUBLISH -
// never contains wildcards itself) is matched by @p filter (a Topic
// Filter, which may) - MQTT 3.1.1 section 4.7. A topic starting with '$'
// (broker-internal, e.g. $SYS/...) only matches a filter that itself
// starts with '$' - a bare wildcard first character never matches one,
// even though '+'/'#' would otherwise be happy to.
static bool _net_mqtt_topic_filter_matches(const char *filter,
                                           const char *topic_name) {
  if ((topic_name[0] == '$') != (filter[0] == '$')) {
    return false;
  }

  while (*filter != '\0' && *topic_name != '\0') {
    if (*filter == '+') {
      // Matches exactly one level - skip topic_name up to (not
      // including) the next '/' or its end, then keep comparing past
      // the '+' itself.
      filter++;
      while (*topic_name != '\0' && *topic_name != '/') {
        topic_name++;
      }
      continue;
    }
    if (*filter == '#') {
      return true; // Matches everything remaining, including zero levels.
    }
    if (*filter != *topic_name) {
      return false;
    }
    filter++;
    topic_name++;
  }

  if (*filter == '\0' && *topic_name == '\0') {
    return true;
  }
  // "sport/#" matches "sport" itself, not just "sport/anything" - the
  // one case where a shorter topic_name can still match a longer filter.
  return *topic_name == '\0' && strcmp(filter, "/#") == 0;
}

// Finds the first confirmed subscription whose filter matches @p
// topic_name - see _net_mqtt_topic_filter_matches()'s own doc. Used by
// _net_mqtt_poll_read_publish() to report which subscription an incoming
// PUBLISH belongs to. If more than one confirmed filter matches (legal -
// e.g. "sport/#" and "sport/+/player1" could both match the same
// topic_name), only the first one found is reported; this client fires
// one net_mqtt_event_received per incoming PUBLISH regardless of how
// many of its own subscriptions actually matched, unlike a broker that
// might count each as a separate delivery. Caller must already hold the
// lock.
// @return The matching topic_id, or `0` if none matched.
uint32_t _net_mqtt_topic_id_for_name(net_mqtt_t *mqtt, const char *topic_name) {
  for (size_t i = 0; i < NET_MQTT_TOPIC_CAPACITY; i++) {
    if (mqtt->topics[i].active && mqtt->topics[i].confirmed &&
        _net_mqtt_topic_filter_matches(mqtt->topics[i].filter, topic_name)) {
      return mqtt->topics[i].topic_id;
    }
  }
  return 0;
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
      (qos != net_mqtt_qos_0 && qos != net_mqtt_qos_1)) {
    return 0; // QoS 2 isn't implemented yet - see net_mqtt_subscribe()'s
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
  while (mqtt->connected && mqtt->subscribe.state != _net_mqtt_subscribe_idle) {
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

bool net_mqtt_unsubscribe(net_mqtt_t *mqtt, uint32_t topic_id) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic_id == 0) {
    return false;
  }

  sys_mutex_lock(mqtt->lock);

  // Wait for either a free unsubscribe slot or a disconnect
  uint32_t timeout_ms = mqtt->timeout_ms;
  uint64_t wait_start_ms = sys_timestamp_ms();
  while (mqtt->connected &&
         mqtt->unsubscribe.state != _net_mqtt_unsubscribe_idle) {
    uint64_t elapsed_ms = sys_timestamp_ms() - wait_start_ms;
    if (timeout_ms == 0 || elapsed_ms >= timeout_ms ||
        !sys_cond_timedwait(mqtt->publish_cond, mqtt->lock,
                            timeout_ms - (uint32_t)elapsed_ms)) {
      sys_mutex_unlock(mqtt->lock);
      return false; // Timed out (or timeout_ms == 0) still waiting for a
                    // free slot.
    }
  }

  if (!mqtt->connected) {
    sys_mutex_unlock(mqtt->lock);
    return false;
  }

  _net_mqtt_topic_t *topic_slot = _net_mqtt_topic_find(mqtt, topic_id);
  if (topic_slot == NULL) {
    sys_mutex_unlock(mqtt->lock);
    return false; // Not a currently confirmed subscription.
  }

  mqtt->unsubscribe.state = _net_mqtt_unsubscribe_requesting;
  sys_sprintf(mqtt->unsubscribe.filter, sizeof(mqtt->unsubscribe.filter), "%s",
              topic_slot->filter);
  mqtt->unsubscribe.topic_id = topic_id;
  mqtt->unsubscribe.packet_id = _net_mqtt_next_packet_id(mqtt);
  mqtt->unsubscribe.topic = topic_slot;

  sys_mutex_unlock(mqtt->lock);
  return true;
}

const char *net_mqtt_topic_to_string(net_mqtt_t *mqtt, uint32_t topic_id) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || topic_id == 0) {
    return NULL;
  }
  sys_mutex_lock(mqtt->lock);
  _net_mqtt_topic_t *topic = _net_mqtt_topic_find(mqtt, topic_id);
  const char *filter = topic != NULL ? topic->filter : NULL;
  sys_mutex_unlock(mqtt->lock);
  return filter;
}

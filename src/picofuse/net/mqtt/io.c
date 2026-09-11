#include "private.h"

// Same polling granularity ntp.c's own read loop uses.
#define _NET_MQTT_POLL_MS 20

void _net_mqtt_fire_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event) {
  // Snapshot under the lock (same one connect.c/publish.c hold for their
  // own I/O - see net_mqtt_set_callback()'s matching write), then call
  // the callback itself outside it - a callback that calls back into
  // net_mqtt_connect()/_publish()/etc. must not deadlock on a lock this
  // function is still holding.
  sys_mutex_lock(mqtt->lock);
  net_mqtt_event_callback_t callback = mqtt->callback;
  void *userdata = mqtt->userdata;
  sys_mutex_unlock(mqtt->lock);

  if (callback != NULL) {
    callback(mqtt, event, userdata);
  }
}

void _net_mqtt_abort_connection_locked(net_mqtt_t *mqtt) {
  if (!mqtt->connected) {
    return;
  }
  sys_iostream_close(mqtt->conn);
  mqtt->conn = NULL;
  mqtt->connected = false;

  // Whatever net_mqtt_publish()/net_mqtt_subscribe() staged (if anything)
  // can never be sent on a connection that no longer exists - see their
  // own doc on this being a silent abandonment, no event of its own.
  // Subscribe additionally reserved a topics[] slot at stage time (see
  // _net_mqtt_topic_alloc()'s own doc) - that has to be freed back here
  // too, or it'd stay falsely claimed forever.
  mqtt->publish.state = _net_mqtt_publish_idle;
  _net_mqtt_topic_free(mqtt, mqtt->subscribe.topic);
  mqtt->subscribe.topic = NULL;
  mqtt->subscribe.state = _net_mqtt_subscribe_idle;

  // Wakes a net_mqtt_publish()/net_mqtt_subscribe() call blocked waiting
  // for either a free slot or a disconnect - this is the disconnect
  // it's also watching for (see their own doc). One shared condvar for
  // both - each waiter re-checks its own predicate on waking, so an
  // unrelated broadcast is harmless, just a spurious wake.
  sys_cond_broadcast(mqtt->publish_cond);
}

uint32_t _net_mqtt_next_message_id(net_mqtt_t *mqtt) {
  uint32_t id = ++mqtt->next_message_id;
  if (id == 0) {
    id = ++mqtt->next_message_id;
  }
  return id;
}

uint16_t _net_mqtt_next_packet_id(net_mqtt_t *mqtt) {
  uint16_t id = ++mqtt->next_packet_id;
  if (id == 0) {
    id = ++mqtt->next_packet_id;
  }
  return id;
}

bool _net_mqtt_write_exact(sys_iostream_t *conn, const void *data, size_t n,
                           uint32_t timeout_ms) {
  if (conn == NULL) {
    return false;
  }
  const char *bytes = data;
  size_t wrote = 0;
  uint64_t start = sys_timestamp_ms();
  while (wrote < n) {
    wrote += sys_iostream_write(conn, bytes + wrote, n - wrote);
    if (wrote < n) {
      if (sys_timestamp_ms() - start >= timeout_ms) {
        return false;
      }
      sys_sleep_ms(_NET_MQTT_POLL_MS);
    }
  }
  return true;
}

bool _net_mqtt_read_exact(sys_iostream_t *conn, void *data, size_t n,
                          uint32_t timeout_ms) {
  if (conn == NULL) {
    return false;
  }
  char *bytes = data;
  size_t got = 0;
  uint64_t start = sys_timestamp_ms();
  while (got < n) {
    got += sys_iostream_read(conn, bytes + got, n - got);
    if (got < n) {
      if (sys_timestamp_ms() - start >= timeout_ms) {
        return false;
      }
      sys_sleep_ms(_NET_MQTT_POLL_MS);
    }
  }
  return true;
}

size_t _net_mqtt_encode_length(uint32_t value, uint8_t *buf, size_t buf_size) {
  if (value > 0x0FFFFFFFu) { // 268,435,455 - MQTT's own 4-byte ceiling
    return 0;
  }
  size_t n = 0;
  do {
    if (n >= buf_size) {
      return 0;
    }
    uint8_t byte = (uint8_t)(value % 128);
    value /= 128;
    if (value > 0) {
      byte |= 0x80;
    }
    buf[n++] = byte;
  } while (value > 0);
  return n;
}
